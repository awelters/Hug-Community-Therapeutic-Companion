#include <ServerProxy.h>

char ServerProxy::WIFI_BOOTUP = '$';
const char ServerProxy::DATA_START[] = {'H', 'T', 'T', 'P', '/', '1', '.', '1', ' ', '2', '0', '7', ' '};
const uint16_t ServerProxy::DATA_START_LENGTH = sizeof(DATA_START) / sizeof(DATA_START[0]);
const char ServerProxy::DATA_END[] = {'\r', '\n'};
const uint16_t ServerProxy::DATA_END_LENGTH = sizeof(DATA_END) / sizeof(DATA_END[0]);

ServerProxy::ServerProxy(Stream *serial, Stream *debug) :
	_serial(serial),
	_debug(debug),
	_state(BOOTING_STATE_FOUND),
	_last_outgoing_sent_time(0),
	_bootup_timeout(0),
	_heartbeat_timeout(0)
{
	resetOutgoing();
	resetIncoming();
}

void ServerProxy::begin() {
	_bootup_timeout = millis();
	bootup();
}

void ServerProxy::bootup() {
	boolean doBootup = false;

	if( _state == CLOSED_STATE_FOUND ) {
		if(_debug != NULL) {
			_debug->println("bootup: closed state found");
		}
		doBootup = true;
	}
	else if( _bootup_timeout != 0 && (millis() - _bootup_timeout) > BOOTUP_WATCHDAWG_SHOULD_BITE ) {
		if(_debug != NULL) {
			_debug->println("bootup: initial bootup sequence watchdawg bit");
		}
		doBootup = true;
	}
	else if( _bootup_timeout == 0 && (millis() - _heartbeat_timeout) > HEARTBEAT_WATCHDAWG_SHOULD_BITE) {
		if(_debug != NULL) {
			_debug->println("bootup: heartbeat watchdawg bit");
		}
		doBootup = true;
	}

	if(doBootup) {
		_serial->write(WIFI_BOOTUP);
		_state = BOOTING_STATE_FOUND;
		_bootup_timeout = 0;
		_heartbeat_timeout = millis();
	}
}

boolean ServerProxy::update() {
	char in;
	boolean serverUpdate = false;
	
	//if there is pending data we should send it
	if(_needToFlush) {
	
		if(_state == OPENED_STATE_FOUND) {
		
			endOutgoing();
			
		}
		else {
			bootup();
		}

	}
	
	//if there is data to be processed then process it
	while(_serial->available() > 0) {

		//heartbeat it
		_heartbeat_timeout = millis();

		//no need for bootup
		_bootup_timeout = 0;

		in = (char)_serial->read();
		
		//returns true if there was a server update
		serverUpdate = setIncoming( in );
			
		//let the world know the truth
		if( serverUpdate ) {
			return true;
		}

	}
	
	return false;
}

//if something to read or something to write then we are processing
boolean ServerProxy::isProcessing() {
	return (_serial->available() > 0) || _needToFlush;
}

boolean ServerProxy::isWaitingToFlush() {
	return _needToFlush;
}

unsigned long ServerProxy::getLastOutgoingSentTime() {
	return _last_outgoing_sent_time;
}

boolean ServerProxy::willOverflowOutgoing(uint32_t numBits) {
	//numBits = ceil(numBits / 8.0) * 8;
	return (_outLengthInBits + numBits) > LAST_BIT_INDEX;
}

boolean ServerProxy::setOutgoing(uint32_t out, uint8_t numBits) {
	//numBits = ceil(numBits / 8.0) * 8;
	
	if(willOverflowOutgoing(numBits)) {
		return false;
	}

	uint8_t byteIndex;
	uint8_t bitIndex;
	uint8_t bitsLeft = numBits;
	
	if(_debug != NULL) {
		_debug->print("outgoing data: ");
		_debug->print(out);
		_debug->println("");
	}
	
	//while the output buffer is not full AND
	//there is still more bits to add
	//NOTE: MSB FIRST, 2 = 01, 4 = 001, etc.
	while(_outLengthInBits < LAST_BIT_INDEX && bitsLeft != 0) {
		byteIndex = floor(_outLengthInBits / 8.0);
		bitIndex = _outLengthInBits % 8;
		
		if(_debug != NULL) {
			_debug->print((out & 1) == 0 ? "0" : "1");
		}
		_out[byteIndex] |= (out & 1) << bitIndex;
		
		//update out by readying the next bit
		out >>= 1;
		
		//update the output buffer length in bits
		_outLengthInBits++;
		
		//update the bits left
		bitsLeft--;
	}
	
	if(_debug != NULL) {
		_debug->println("");
	}

	if(bitsLeft == 0) {
		return true;
	}

	//this should never happen
	return false;
}

void ServerProxy::endOutgoing() {
	uint32_t bits = 0;
	uint8_t bitIndex = 0;
	uint8_t byteIndex = 0;
	uint8_t out = 0;
	uint8_t bit = 0;
	uint8_t bytesWritten = 0;
	uint8_t bytesActuallyWritten = 0;
	
	if(_outLengthInBits == 0) {
		return;
	}

	if( _state != OPENED_STATE_FOUND ) {
		_needToFlush = true;
		return;
	}
	
	while(bits < _outLengthInBits) {
		byteIndex = floor(bits / 8.0);
		
		out = _out[byteIndex];
		bitIndex = 0;
		
		if(_debug != NULL) {
			_debug->print("bits: ");
		}

		while(bitIndex < 8) {
			bit = (out & 1);
			
			if(_debug != NULL) {
				_debug->print(bit == 0 ? "0" : "1");
			}

			out >>= 1;
			bitIndex++;
		}
		if(_debug != NULL) {
			_debug->println("");
		}

		bits+=8;
	}
	
	if(_debug != NULL) {
		_debug->println("");
	}

	if(_debug != NULL) {
		_debug->print("bits length: ");
		_debug->println(_outLengthInBits);
	}
	
	bytesWritten = ceil(_outLengthInBits/8.0);
	
	if(_debug != NULL) {
		_debug->print("bytes to write: ");
		_debug->println(bytesWritten);
	}
	
	bytesActuallyWritten = _serial->write(_out, bytesWritten);
	
	if(_debug != NULL) {
		_debug->print("bytes written: ");
		_debug->println(bytesActuallyWritten);
	}
		
	if(bytesActuallyWritten != 0) {
		_last_outgoing_sent_time = millis();
		resetOutgoing();
	}
}

const char* ServerProxy::getIncoming() {
	return (char *)_in;
}

void ServerProxy::resetOutgoing() {
	memset(_out,0,sizeof(_out));
	_outLengthInBits = 0;
	_needToFlush = false;
}

void ServerProxy::resetIncoming() {
	memset(_in,0,sizeof(_in));
	_inLen = 0;
	_inState = COMMAND_NOT_FOUND;
	_commandParsePosition = 0;
}

boolean ServerProxy::setIncoming( char in ) {
	uint16_t position;

	if(_debug != NULL) {
		_debug->print(in);
	}

	if(_inState == COMMAND_NOT_FOUND) {
		//see if incoming matches command syntax
		if(in == DATA_START[_commandParsePosition++]) {
			//if command found then switch incoming state and reset command parse position for the next time
			if(_commandParsePosition == DATA_START_LENGTH) {
				_inState = COMMAND_FOUND;
			}
		} //the data does not match the protocol, so start looking for the command again
		else {
			_commandParsePosition = 0;
			if(in == WIFI_START) {
				_inState = WIFI_STATE_FOUND;
			}
		}
	} 
	else if(_inState == WIFI_STATE_FOUND) {
		//check default wifi states
		switch(in) {
			case OPENED : _state = OPENED_STATE_FOUND; if(_debug != NULL) _debug->println("OPENED"); break;
			case CLOSED : _state = CLOSED_STATE_FOUND; if(_debug != NULL) _debug->println("CLOSED"); break;
		}
		_commandParsePosition = 0; 
		_inState = COMMAND_NOT_FOUND;
	} //we can't handle anymore, doesn't follow protocol, so start looking for the command again
	else if(_inLen > LAST_INDEX) {
		resetIncoming();
	} //else collect the data
	else if(in != DATA_END[0]) {
		_in[_inLen++] = in;
	}
	else {
		//data found and at the end of the data
		_inState = COMMAND_NOT_FOUND;
		_commandParsePosition = 0;
		if(_debug != NULL) {
			_debug->println("");
			_debug->print("COMMAND FOUND: ");
			_debug->write(_in, _inLen);
			_debug->println("");
		}
		return true;
	}
	return false;
}