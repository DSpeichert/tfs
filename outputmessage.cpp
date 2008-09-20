//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////
#include "otpch.h"

#include "outputmessage.h"
#include "connection.h"
#include "protocol.h"

OutputMessage::OutputMessage()
{
	freeMessage();
}

//*********** OutputMessagePool ****************//

OutputMessagePool::OutputMessagePool()
{
	OTSYS_THREAD_LOCKVARINIT(m_outputPoolLock);
	for(uint32_t i = 0; i < OUTPUT_POOL_SIZE; ++i)
	{
		OutputMessage* msg = new OutputMessage();
		m_outputMessages.push_back(msg);
#ifdef __TRACK_NETWORK__
		m_allOutputMessages.push_back(msg);
#endif
	}

	m_frameTime = OTSYS_TIME();
}

void OutputMessagePool::startExecutionFrame()
{
	m_frameTime = OTSYS_TIME();
}

OutputMessagePool::~OutputMessagePool()
{
	OutputMessageVector::iterator it;
	for(it = m_outputMessages.begin(); it != m_outputMessages.end(); ++it)
		delete *it;

	m_outputMessages.clear();
	OTSYS_THREAD_LOCKVARRELEASE(m_outputPoolLock);
}

void OutputMessagePool::send(OutputMessage* msg)
{
	OTSYS_THREAD_LOCK(m_outputPoolLock, "");
	OutputMessage::OutputMessageState state = msg->getState();
	OTSYS_THREAD_UNLOCK(m_outputPoolLock, "");

	if(state == OutputMessage::STATE_ALLOCATED_NO_AUTOSEND)
	{
		#ifdef __DEBUG_NET_DETAIL__
		std::cout << "Sending message - SINGLE" << std::endl;
		#endif

		if(msg->getConnection())
		{
			if(msg->getConnection()->send(msg))
			{
				//Note: if we ever decide to change how the pool works this will have to change
				OTSYS_THREAD_LOCK(m_outputPoolLock, "");
				if(msg->getState() != OutputMessage::STATE_FREE)
					msg->setState(OutputMessage::STATE_WAITING);
				OTSYS_THREAD_UNLOCK(m_outputPoolLock, "");
			}
			else
				internalReleaseMessage(msg);
		}
		else
		{
			#ifdef __DEBUG_NET__
			std::cout << "Error: [OutputMessagePool::send] NULL connection." << std::endl;
			#endif
		}
	}
	else
	{
		#ifdef __DEBUG_NET__
		std::cout << "Warning: [OutputMessagePool::send] State != STATE_ALLOCATED_NO_AUTOSEND" << std::endl;
		#endif
	}
}

void OutputMessagePool::sendAll()
{
	OTSYS_THREAD_LOCK_CLASS lockClass(m_outputPoolLock);
	for(OutputMessageVector::iterator it = m_autoSendOutputMessages.begin(); it != m_autoSendOutputMessages.end(); )
	{
		#ifdef __NO_PLAYER_SENDBUFFER__
		//use this define only for debugging
		bool v = 1;
		#else
		//It will send only messages bigger then 1 kb or with a lifetime greater than 10 ms
		bool v = (*it)->getMessageLength() > 1024 || (m_frameTime - (*it)->getFrame() > 10);
		#endif
		if(v)
		{
			#ifdef __DEBUG_NET_DETAIL__
			std::cout << "Sending message - ALL" << std::endl;
			#endif

			if((*it)->getConnection())
			{
				if((*it)->getConnection()->send(*it))
				{
					// Note: if we ever decide to change how the pool works this will have to change
					if((*it)->getState() != OutputMessage::STATE_FREE)
						(*it)->setState(OutputMessage::STATE_WAITING);
				}
				else
					internalReleaseMessage(*it);
			}
			else
			{
				#ifdef __DEBUG_NET__
				std::cout << "Error: [OutputMessagePool::send] NULL connection." << std::endl;
				#endif
			}
			m_autoSendOutputMessages.erase(it++);
		}
		else
			++it;
	}
}

void OutputMessagePool::internalReleaseMessage(OutputMessage* msg)
{
	//Simulate that the message is sent and then liberate it
	msg->getProtocol()->onSendMessage(msg);
	msg->freeMessage();
	m_outputMessages.push_back(msg);
}

void OutputMessagePool::releaseMessage(OutputMessage* msg, bool sent /*= false*/)
{
	OTSYS_THREAD_LOCK_CLASS lockClass(m_outputPoolLock);
	switch(msg->getState())
	{
		case OutputMessage::STATE_ALLOCATED:
		{
			OutputMessageVector::iterator it =
				std::find(m_autoSendOutputMessages.begin(), m_autoSendOutputMessages.end(), msg);
			if(it != m_autoSendOutputMessages.end())
				m_autoSendOutputMessages.erase(it);
			msg->freeMessage();
			m_outputMessages.push_back(msg);
			break;
		}
		case OutputMessage::STATE_ALLOCATED_NO_AUTOSEND:
			msg->freeMessage();
			m_outputMessages.push_back(msg);
			break;
		case OutputMessage::STATE_WAITING:
			if(!sent)
				std::cout << "Error: [OutputMessagePool::releaseMessage] Releasing STATE_WAITING OutputMessage." << std::endl;
			else
			{
				msg->freeMessage();
				m_outputMessages.push_back(msg);
			}
			break;
		case OutputMessage::STATE_FREE:
			std::cout << "Error: [OutputMessagePool::releaseMessage] Releasing STATE_FREE OutputMessage." << std::endl;
			break;
		default:
			std::cout << "Error: [OutputMessagePool::releaseMessage] Releasing STATE_?(" << msg->getState() <<") OutputMessage." << std::endl;
			break;
	}
}

OutputMessage* OutputMessagePool::getOutputMessage(Protocol* protocol, bool autosend /*= true*/)
{
	#ifdef __DEBUG_NET__
	if(protocol->getConnection() == NULL)
		std::cout << "Warning: [OutputMessagePool::getOutputMessage] NULL connection." << std::endl;
	#endif
	#ifdef __DEBUG_NET_DETAIL__
	std::cout << "request output message - auto = " << autosend << std::endl;
	#endif

	OTSYS_THREAD_LOCK_CLASS lockClass(m_outputPoolLock);
	OutputMessage* outputmessage;
	if(m_outputMessages.empty())
	{
#ifdef __TRACK_NETWORK__
		if(m_allOutputMessages.size() >= 5000)
		{
			std::cout << "High usage of outputmessages: " << std::endl;
			m_allOutputMessages.back()->PrintTrace();
		}
#endif
		outputmessage = new OutputMessage;
#ifdef __TRACK_NETWORK__
		m_allOutputMessages.push_back(outputmessage);
#endif
	}
	else
	{
		outputmessage = m_outputMessages.back();
#ifdef __TRACK_NETWORK__
		// Print message trace
		if(outputmessage->getState() != OutputMessage::STATE_FREE)
		{
			std::cout << "Using allocated message, message trace:" << std::endl;
			outputmessage->PrintTrace();
		}
#else
		assert(outputmessage->getState() == OutputMessage::STATE_FREE);
#endif
		m_outputMessages.pop_back();
	}

	configureOutputMessage(outputmessage, protocol, autosend);
	return outputmessage;
}

void OutputMessagePool::configureOutputMessage(OutputMessage* msg, Protocol* protocol, bool autosend)
{
	msg->Reset();
	if(autosend)
	{
		msg->setState(OutputMessage::STATE_ALLOCATED);
		m_autoSendOutputMessages.push_back(msg);
	}
	else
		msg->setState(OutputMessage::STATE_ALLOCATED_NO_AUTOSEND);

	msg->setProtocol(protocol);
	msg->setConnection(protocol->getConnection());
	msg->setFrame(m_frameTime);
}
