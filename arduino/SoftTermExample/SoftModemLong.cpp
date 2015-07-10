#include "SoftModemLong.h"

#define TX_PIN      (3)
#define RX_PIN1     (6)  // AIN0
#define RX_PIN2     (7)  // AIN1

SoftModemLong *SoftModemLong::activeObject = 0;

SoftModemLong::SoftModemLong() {
}

SoftModemLong::~SoftModemLong() {
	end();
}

#if F_CPU == 16000000
#if SOFT_MODEM_BAUD_RATE <= 126
  #define TIMER_CLOCK_SELECT       (7)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(1024))
#elif SOFT_MODEM_BAUD_RATE <= 315
  #define TIMER_CLOCK_SELECT       (6)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(256))
#elif SOFT_MODEM_BAUD_RATE <= 630
  #define TIMER_CLOCK_SELECT       (5)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(128))
#elif SOFT_MODEM_BAUD_RATE <= 1225
  #define TIMER_CLOCK_SELECT       (4)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(64))
#else
  #define TIMER_CLOCK_SELECT       (3)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(32))
#endif
#else
#if SOFT_MODEM_BAUD_RATE <= 126
  #define TIMER_CLOCK_SELECT       (6)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(256))
#elif SOFT_MODEM_BAUD_RATE <= 315
  #define TIMER_CLOCK_SELECT       (5)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(128))
#elif SOFT_MODEM_BAUD_RATE <= 630
  #define TIMER_CLOCK_SELECT       (4)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(64))
#else
  #define TIMER_CLOCK_SELECT       (3)
  #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(32))
#endif
#endif

//time length of a bit in transmission (microseconds unit) - baud rate: bit per second
#define BIT_PERIOD             (1000000/SOFT_MODEM_BAUD_RATE)

//period of high frequence waveform (in microseconds) - time length of a full-cycle
#define HIGH_FREQ_MICROS       (1000000/SOFT_MODEM_HIGH_FREQ)
//period of low frequence waveform (in microseconds) - time length of a full-cycle
#define LOW_FREQ_MICROS        (1000000/SOFT_MODEM_LOW_FREQ)

//number of full-period (full-cycle) hight frequency waveform in 1 HIGH bit
#define HIGH_FREQ_CNT          (BIT_PERIOD/HIGH_FREQ_MICROS)
//number of full-period (full-cycle) low frequency waveform in 1 LOW bit
#define LOW_FREQ_CNT           (BIT_PERIOD/LOW_FREQ_MICROS)

//number of bits transfer in 40ms
#define MAX_CARRIR_BITS        (40000/BIT_PERIOD) // 40ms

//number of timer count in 1 bit period
#define TCNT_BIT_PERIOD        (BIT_PERIOD/MICROS_PER_TIMER_COUNT)
//number of timer count in 1 full-cycle high frequency waveform
#define TCNT_HIGH_FREQ         (HIGH_FREQ_MICROS/MICROS_PER_TIMER_COUNT)
//number of timer count in 1 full-cycle low frequency waveform
#define TCNT_LOW_FREQ          (LOW_FREQ_MICROS/MICROS_PER_TIMER_COUNT)

//lower threshold for timer count for high freq
#define TCNT_HIGH_TH_L         (TCNT_HIGH_FREQ * 0.80)
//upper threshold for timer count for high freq
#define TCNT_HIGH_TH_H         (TCNT_HIGH_FREQ * 1.15)

//lower threshold for timer count for low freq
#define TCNT_LOW_TH_L          (TCNT_LOW_FREQ * 0.85)
//upper threshold for timer count for low freq
#define TCNT_LOW_TH_H          (TCNT_LOW_FREQ * 1.20)

#if SOFT_MODEM_DEBUG_ENABLE
static volatile uint8_t *_portLEDReg;
static uint8_t _portLEDMask;
#endif

enum { START_BIT = 0, DATA_BIT = 8, STOP_BIT = 9, INACTIVE = 0xff };

void SoftModemLong::begin(void)
{
	//set received pin RX_PIN1 to INPUT mode, set it to LOW
	pinMode(RX_PIN1, INPUT);
	digitalWrite(RX_PIN1, LOW);

	//set received pin RX_PIN2 to INPUT mode, set it to LOW
	pinMode(RX_PIN2, INPUT);
	digitalWrite(RX_PIN2, LOW);

	//set transmit pin TX_PIN to OUTPUT mode, set it to LOW
	pinMode(TX_PIN, OUTPUT);
	digitalWrite(TX_PIN, LOW);

	//find out Port Register for transmit pin TX_PIN
	// digitalPinToPort returns name of the port that TX_PIN belongs
	// portOutputRegister returns register that used to control port that TX_PIN belongs
	// which in turn control TX_PIN value
	_txPortReg = portOutputRegister(digitalPinToPort(TX_PIN));

	//find the bitmask used to set transmit pin TX_PIN in port register
	_txPortMask = digitalPinToBitMask(TX_PIN);

#if SOFT_MODEM_DEBUG_ENABLE
	_portLEDReg = portOutputRegister(digitalPinToPort(13));
	_portLEDMask = digitalPinToBitMask(13);
    pinMode(13, OUTPUT);
#endif

	_recvStat = INACTIVE;
	//reset ring buffer of received buffer
	_recvBufferHead = _recvBufferTail = 0;

	SoftModemLong::activeObject = this;

	//assign _lastTCNT to timer counter register TCNT2, TCNT2 increase by 1 for each timer clock,
	// time clock can be set by prescale in timer counter control register TCCR2A & TCCR2B --> refer Datasheet
	_lastTCNT = TCNT2;
	_lastDiff = _lowCount = _highCount = 0;

	//set all bit in timer counter control register TCCR2A to 0
	TCCR2A = 0;

	//set timer counter contrl register TCCR2B to TIMER_CLOCK_SELECT
	// TIMER_CLOCK_SELECT will decide prescale for counting --> Refer Datasheet ATMEGA328P in p158
	// TIMER_CLOCK_SELECT is defined above base on F_CPU (CPU frequency) and baud rate
	// in case of Arduino Uno, F_CPU = 16MHz, if we set baud rate to 1225, then we have
	// #define TIMER_CLOCK_SELECT       (4)
	// #define MICROS_PER_TIMER_COUNT   (clockCyclesToMicroseconds(64))
	// according to Datasheet, if TCCR2B is set to 4 (0b00000100), then it is prescaled 64 times (64 clocks for +1 count)
	// that is the reason that MICROS_PER_TIMER_COUNT = clockCyclesToMicroseconds(64)
	// F_CPU = 16MHz --> 16 clk is 1us
	// Prescale = 64 --> 64 clk is 1 count
	// --> each increment of timer counter equal to 64/16 = 4us pass --> clockCyclesToMicroseconds(64)
	TCCR2B = TIMER_CLOCK_SELECT;

	// ACSR: Analog Comparator Control and Status Register (Datasheet p240)
	// ACIE: name of bit in ACSR which control Analog Comparator Interrupt Enable
	// ACIS1: name of bit in ACSR which is Analog Comparator Interrupt Mode Select. 
	//   This bit determine which comparator events that trigger the Analog Comparator interrupt.
	// here we set ACSR = 0b00001010 --> Comparator Interrupt on Falling Output Edge.
	ACSR   = _BV(ACIE) | _BV(ACIS1);

	// digital port off --> save power consumption, only need analog input value (Datasheet p241)
	DIDR1  = _BV(AIN1D) | _BV(AIN0D); 
}

void SoftModemLong::end(void)
{
	ACSR   &= ~(_BV(ACIE));
	TIMSK2 &= ~(_BV(OCIE2A));
	DIDR1  &= ~(_BV(AIN1D) | _BV(AIN0D));
	SoftModemLong::activeObject = 0;
}

void SoftModemLong::demodulate(void)
{
	uint8_t t = TCNT2;
	uint8_t diff;
	
	if(TIFR2 & _BV(TOV2)){
		TIFR2 |= _BV(TOV2);
		diff = (255 - _lastTCNT) + t + 1;
	}
	else{
		diff = t - _lastTCNT;
	}
	
	if(diff < (uint8_t)(TCNT_HIGH_TH_L))				// Noise?
		return;
	
	_lastTCNT = t;
	
	if(diff > (uint8_t)(TCNT_LOW_TH_H))
		return;
	
//	_lastDiff = (diff >> 1) + (diff >> 2) + (_lastDiff >> 2);
    _lastDiff = diff;
	
	if(_lastDiff >= (uint8_t)(TCNT_LOW_TH_L)){
		_lowCount += _lastDiff;
		if((_recvStat == INACTIVE) && (_lowCount >= (uint8_t)(TCNT_BIT_PERIOD * 0.5))){ // maybe Start-Bit
			_recvStat  = START_BIT;
			_highCount = 0;
			_recvBits  = 0;
			OCR2A      = t + (uint8_t)(TCNT_BIT_PERIOD) - _lowCount; // 1 bit period after detected
			TIFR2     |= _BV(OCF2A);
			TIMSK2    |= _BV(OCIE2A);
		}
	}
	else if(_lastDiff <= (uint8_t)(TCNT_HIGH_TH_H)){
		_highCount += _lastDiff;
		if((_recvStat == INACTIVE) && (_highCount >= (uint8_t)(TCNT_BIT_PERIOD))){
			_lowCount = _highCount = 0;
		}
	}
}

ISR(ANALOG_COMP_vect)
{
	SoftModemLong::activeObject->demodulate();
}

void SoftModemLong::recv(void)
{
	uint8_t high;
	if(_highCount > _lowCount){
 		if(_highCount >= (uint8_t)TCNT_BIT_PERIOD)
 			_highCount -= (uint8_t)TCNT_BIT_PERIOD;
		else
			_highCount = 0;
		high = 0x80;
	}
	else{
 		if(_lowCount >= (uint8_t)TCNT_BIT_PERIOD)
 			_lowCount -= (uint8_t)TCNT_BIT_PERIOD;
 		else
			_lowCount = 0;
		high = 0x00;
	}
	
	if(_recvStat == START_BIT){	// Start bit
		if(!high){
			_recvStat++;
		}else{
			goto end_recv;
		}
	}
	else if(_recvStat <= DATA_BIT) { // Data bits
		_recvBits >>= 1;
		_recvBits |= high;
		_recvStat++;
	}
	else if(_recvStat == STOP_BIT){	// Stop bit
		uint8_t new_tail = (_recvBufferTail + 1) & (SOFT_MODEM_RX_BUF_SIZE - 1);
		if(new_tail != _recvBufferHead){
			_recvBuffer[_recvBufferTail] = _recvBits;
			_recvBufferTail = new_tail;
		}
		goto end_recv;
	}
	else{
	end_recv:
		_recvStat = INACTIVE;
		TIMSK2 &= ~_BV(OCIE2A);
	}
}

ISR(TIMER2_COMPA_vect)
{
	OCR2A += (uint8_t)TCNT_BIT_PERIOD;
	SoftModemLong::activeObject->recv();
#if SOFT_MODEM_DEBUG_ENABLE
	*_portLEDReg ^= _portLEDMask;
#endif  
}

int SoftModemLong::available()
{
	return (_recvBufferTail + SOFT_MODEM_RX_BUF_SIZE - _recvBufferHead) & (SOFT_MODEM_RX_BUF_SIZE - 1);
}

int SoftModemLong::read()
{
	if(_recvBufferHead == _recvBufferTail)
		return -1;
	int d = _recvBuffer[_recvBufferHead];
	_recvBufferHead = (_recvBufferHead + 1) & (SOFT_MODEM_RX_BUF_SIZE - 1);
	return d;
}

int SoftModemLong::peek()
{
	if(_recvBufferHead == _recvBufferTail)
		return -1;
	return _recvBuffer[_recvBufferHead];
}

void SoftModemLong::flush()
{
	_recvBufferHead = _recvBufferTail = 0;
}

void SoftModemLong::modulate(uint8_t b)
{
	uint8_t cnt,tcnt,tcnt2;
	if(b){ //if modulated bit is HIGH

		//number of full-period (full-cycle) hight frequency waveform
		// in 1 HIGH bit
		cnt = (uint8_t)(HIGH_FREQ_CNT);

		//timer count in half-cycle high freq waveform
		tcnt2 = (uint8_t)(TCNT_HIGH_FREQ / 2);

		//timer count in half-cycle high freq waveform
		// we use substraction because it is neccessary to satisfy
		// tcnt + tcnt2 == TCNT_HIGH_FREQ
		tcnt = (uint8_t)(TCNT_HIGH_FREQ) - tcnt2;

	}else{ //if modulated bit is LOW

		cnt = (uint8_t)(LOW_FREQ_CNT);
		tcnt2 = (uint8_t)(TCNT_LOW_FREQ / 2);
		tcnt = (uint8_t)(TCNT_LOW_FREQ) - tcnt2;
	}
	do {
		cnt--;
		
		/*=== The first half-cycle waveform ===*/
		{ 
			//TCNT2 timer counter will be continously counting
			// value in OCR2B will be continously compared to TCNT2

			//set OCR2B to timer count number of first-half-cycle waveform
			OCR2B += tcnt;

			//set flag in TIFR2 (Timer Counter2 Interrupt Flag Register)
			// this OCF2B bit in TIFR2 will be cleared after there is a match
			// in comparing TCNT2 and OCR2B
			TIFR2 |= _BV(OCF2B);

			//loop until there is a match in comparing TCNT2 and OCR2B
			// when OCR2B == TCNT2, bit OCF2B in TIFR2 will be cleard
			while(!(TIFR2 & _BV(OCF2B)));
		}
		//When there is match in comparing OCR2B and TCNT2,
		// invert output value of transmit output pin
		*_txPortReg ^= _txPortMask;

		/*=== The second half-cycle waveform ===*/
		{ 
			//set OCR2B to timer count number of first-half-cycle waveform
			OCR2B += tcnt2;

			//set flag in TIFR2 (Timer Counter2 Interrupt Flag Register)
			// this OCF2B bit in TIFR2 will be cleared after there is a match
			// in comparing TCNT2 and OCR2B
			TIFR2 |= _BV(OCF2B);

			//loop until there is a match in comparing TCNT2 and OCR2B
			// when OCR2B == TCNT2, bit OCF2B in TIFR2 will be cleard
			while(!(TIFR2 & _BV(OCF2B)));
		}
		//When there is match in comparing OCR2B and TCNT2, 
		// invert output value of transmit output pin
		*_txPortReg ^= _txPortMask;
	} while (cnt);
}

//  Brief carrier tone before each transmission
//  1 start bit (LOW)
//  8 data bits, LSB first
//  1 stop bit (HIGH)
//  ...
//  1 push bit (HIGH)

size_t SoftModemLong::write(const uint8_t *buffer, size_t size)
{
    uint8_t cnt = ((micros() - _lastWriteTime) / BIT_PERIOD) + 1;
    if(cnt > MAX_CARRIR_BITS)
        cnt = MAX_CARRIR_BITS;
    for(uint8_t i = 0; i<cnt; i++)
        modulate(HIGH);
    size_t n = size;
    while (size--) {
        uint8_t data = *buffer++;
        modulate(LOW);							 // Start Bit
        for(uint8_t mask = 1; mask; mask <<= 1){ // Data Bits
            if(data & mask){
                modulate(HIGH);
            }
            else{
                modulate(LOW);
            }
        }
        modulate(HIGH);				// Stop Bit
    }
	modulate(HIGH);				// Push Bit
    _lastWriteTime = micros();
    return n;
}

size_t SoftModemLong::write(uint8_t data)
{
    return write(&data, 1);
}

size_t SoftModemLong::writeLong(const uint32_t *buffer, size_t size)
{
	uint8_t cnt = ((micros() - _lastWriteTime) / BIT_PERIOD) + 1;
    if(cnt > MAX_CARRIR_BITS)
        cnt = MAX_CARRIR_BITS;
    for(uint8_t i = 0; i<cnt; i++)
        modulate(HIGH);
    size_t n = size;
    while (size--) {
        uint32_t data = *buffer++;
        modulate(LOW);							 // Start Bit
        for(uint32_t mask = 1; mask; mask <<= 1){ // Data Bits
            if(data & mask){
                modulate(HIGH);
            }
            else{
                modulate(LOW);
            }
        }
        modulate(HIGH);				// Stop Bit
    }
	modulate(HIGH);				// Push Bit
    _lastWriteTime = micros();
    return n;
}

size_t SoftModemLong::writeLong(uint32_t data)
{
	return writeLong(&data, 4);
}
