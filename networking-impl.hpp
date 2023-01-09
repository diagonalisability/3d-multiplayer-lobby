#include<cstring> // std::memmove
#include<unistd.h>
#include"memcpy.hpp"
#include"networking.hpp"
/* handleMessage
	- should look like
		(MessageType messageType, char const *scanPos, Size remainingByteC) -> <some integral type>
	- it should return -1 if there isn't enough space to read the message,
		or the message length if the message was read and processed
*/
template<typename HandleMessage, typename HandleEndOfStream>
void handleMessageStreamReadable(
	signed const fd,
	AsyncRead &asyncRead,
	HandleMessage &&handleMessage,
	HandleEndOfStream &&handleEndOfStream
) {
//	std::cout << "start handleMessageStreamReadable...\n";
	char messages[maxMessageLength * maxMessagesToReceiveAtOnce];
	U32F incompleteMessageLength= asyncRead.incompleteMessageLength;
	char *incompleteMessage= asyncRead.incompleteMessage;
	for(;;) {
		std::memmove(messages, incompleteMessage, incompleteMessageLength);
		// read new messages
		ssize_t const readRet= read(
			fd,
			messages + incompleteMessageLength,
			sizeof messages - incompleteMessageLength
		);
		// check for end of stream or read error
		if(-1 == readRet) {
			if(EAGAIN == errno || EWOULDBLOCK == errno) {
				asyncRead.incompleteMessageLength= incompleteMessageLength;
				std::memcpy(
					asyncRead.incompleteMessage,
					messages,
					incompleteMessageLength
				);
				break;
			}
		}
		if(-1 == readRet && ECONNRESET == errno || 0 == readRet) {
			handleEndOfStream();
			break;
		}
		PERROR_ASSERT(0 < readRet);
		// handle all complete messages in $messages
		U32F const filledByteC= incompleteMessageLength + readRet;
		for(U32F scanI= 0;;) {
			char *const scanPos= messages + scanI;
			U32F const remainingByteC= filledByteC - scanI;
			// store the incomplete message for later
			auto const &handleIncompleteMessage= [
				&incompleteMessageLength, &incompleteMessage,
				remainingByteC, scanPos
			]{
				incompleteMessageLength= remainingByteC;
				incompleteMessage= scanPos;
			};
			if(remainingByteC < sizeof(MessageType)) {
				handleIncompleteMessage();
				break;
			}
			MessageType const messageType= [scanPos]{
				if constexpr(std::is_same_v<MessageType, char>)
					return *scanPos;
				else {
					MessageType ret;
					memcpyInit(ret, &scanPos);
					return ret;
				}
			}();
			// actually handle a message
			auto const handleMessageRet= handleMessage(
				messageType,
				static_cast<char const*>(scanPos) + sizeof messageType,
				remainingByteC - sizeof messageType
			);
			if(handleMessageRet == static_cast<decltype(handleMessageRet)>(-1)) {
				handleIncompleteMessage();
				break;
			}
			ASSERT(0 <= handleMessageRet);
			scanI += sizeof messageType + handleMessageRet;
		}
	}
}
