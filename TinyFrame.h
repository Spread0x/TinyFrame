#ifndef TinyFrameH
#define TinyFrameH

//---------------------------------------------------------------------------
#include <stdint.h>  // for uint8_t etc
#include <stdbool.h> // for bool
#include <stdlib.h>  // for NULL
//#include "messages.h" // for your message IDs (enum or defines)
//#include <esp8266.h> // when using with esphttpd
//---------------------------------------------------------------------------


//----------------------------- PARAMETERS ----------------------------------

// Maximum send / receive payload size (static buffers size)
// Larger payloads will be rejected.
#define TF_MAX_PAYLOAD_RX 1024
#define TF_MAX_PAYLOAD_TX 1024

// --- Listener counts - determine sizes of the static slot tables ---

// Frame ID listeners (wait for response / multi-part message)
#define TF_MAX_ID_LST   20
// Frame Type listeners (wait for frame with a specific first payload byte)
#define TF_MAX_TYPE_LST 20
// Generic listeners (fallback if no other listener catches it)
#define TF_MAX_GEN_LST  4

// Timeout for receiving & parsing a frame
// ticks = number of calls to TF_Tick()
#define TF_PARSER_TIMEOUT_TICKS 10

//----------------------------- FRAME FORMAT ---------------------------------
// The format can be adjusted to fit your particular application needs

// If the connection is reliable, you can disable the SOF byte and checksums.
// That can save up to 9 bytes of overhead.

// ,-----+----+-----+------+------------+- - - -+------------,
// | SOF | ID | LEN | TYPE | HEAD_CKSUM | DATA  | PLD_CKSUM  |
// | 1   | ?  | ?   | ?    | ?          | ...   | ?          | <- size (bytes)
// '-----+----+-----+------+------------+- - - -+------------'

// !!! BOTH SIDES MUST USE THE SAME SETTINGS !!!

// Adjust sizes as desired (1,2,4)
#define TF_ID_BYTES     1
#define TF_LEN_BYTES    2
#define TF_TYPE_BYTES   1

// Select checksum type (0 = none, 8 = ~XOR, 16 = CRC16 0x8005, 32 = CRC32)
#define TF_CKSUM_TYPE   16

// Use a SOF byte to mark the start of a frame
#define TF_USE_SOF_BYTE 1
// Value of the SOF byte (if TF_USE_SOF_BYTE == 1)
#define TF_SOF_BYTE     0x01

// used for timeout tick counters - should be large enough for all used timeouts
typedef uint16_t TF_TICKS;

// used in loops iterating over listeners
typedef uint8_t TF_COUNT;

//------------------------- End of user config ------------------------------


//region Resolve data types

#if TF_LEN_BYTES == 1
	typedef uint8_t TF_LEN;
#elif TF_LEN_BYTES == 2
	typedef uint16_t TF_LEN;
#elif TF_LEN_BYTES == 4
	typedef uint32_t TF_LEN;
#else
	#error Bad value of TF_LEN_BYTES, must be 1, 2 or 4
#endif


#if TF_TYPE_BYTES == 1
	typedef uint8_t TF_TYPE;
#elif TF_TYPE_BYTES == 2
	typedef uint16_t TF_TYPE;
#elif TF_TYPE_BYTES == 4
	typedef uint32_t TF_TYPE;
#else
	#error Bad value of TF_TYPE_BYTES, must be 1, 2 or 4
#endif


#if TF_ID_BYTES == 1
	typedef uint8_t TF_ID;
#elif TF_ID_BYTES == 2
	typedef uint16_t TF_ID;
#elif TF_ID_BYTES == 4
	typedef uint32_t TF_ID;
#else
	#error Bad value of TF_ID_BYTES, must be 1, 2 or 4
#endif


#if TF_CKSUM_TYPE == 8 || TF_CKSUM_TYPE == 0
	// ~XOR (if 0, still use 1 byte - it won't be used)
	typedef uint8_t TF_CKSUM;
#elif TF_CKSUM_TYPE == 16
	// CRC16
	typedef uint16_t TF_CKSUM;
#elif TF_CKSUM_TYPE == 32
	// CRC32
	typedef uint32_t TF_CKSUM;
#else
	#error Bad value for TF_CKSUM_TYPE, must be 8, 16 or 32
#endif


// Bytes added to TF_MAX_PAYLOAD for the send buffer size.
#define TF_OVERHEAD_BYTES \
	(1*TF_USE_SOF_BYTE + \
		sizeof(TF_ID) + \
		sizeof(TF_LEN) + \
		sizeof(TF_CKSUM) + \
		sizeof(TF_TYPE) + \
		sizeof(TF_CKSUM) \
	)

//endregion

//---------------------------------------------------------------------------

// Type-dependent masks for bit manipulation in the ID field
#define TF_ID_MASK (TF_ID)(((TF_ID)1 << (sizeof(TF_ID)*8 - 1)) - 1)
#define TF_ID_PEERBIT (TF_ID)((TF_ID)1 << ((sizeof(TF_ID)*8) - 1))

//---------------------------------------------------------------------------

/** Peer bit enum (used for init) */
typedef enum {
	TF_SLAVE = 0,
	TF_MASTER = 1
} TF_PEER;

/** Data structure for sending / receiving messages */
typedef struct _TF_MSG_STRUCT_ {
	TF_ID frame_id;       // message ID
	bool is_response;     // internal flag, set when using the Respond function. frame_id is then kept unchanged.
	TF_TYPE type;         // received or sent message type
	const uint8_t *data;  // buffer of received data or data to send. NULL = listener timed out, free userdata!
	TF_LEN len;           // length of the buffer
	void *userdata;       // here's a place for custom data; this data will be stored with the listener
} TF_MSG;

/**
 * Clear message struct
 */
static inline void TF_ClearMsg(TF_MSG *msg)
{
	msg->frame_id = 0;
	msg->is_response = 0;
	msg->type = 0;
	msg->data = NULL;
	msg->len = 0;
	msg->userdata = NULL;
}

/**
 * TinyFrame Type Listener callback
 *
 * @param frame_id - ID of the received frame
 * @param type - type field from the message
 * @param data - byte buffer with the application data
 * @param len - number of bytes in the buffer
 * @return true if the frame was consumed
 */
typedef bool (*TF_LISTENER)(TF_MSG *msg);

/**
 * Initialize the TinyFrame engine.
 * This can also be used to completely reset it (removing all listeners etc)
 *
 * @param peer_bit - peer bit to use for self
 */
void TF_Init(TF_PEER peer_bit);

/**
 * Reset the frame parser state machine.
 * This does not affect registered listeners.
 */
void TF_ResetParser(void);

/**
 * Register a frame type listener.
 *
 * @param msg - message (contains frame_id and userdata)
 * @param cb - callback
 * @param timeout - timeout in ticks to auto-remove the listener (0 = keep forever)
 * @return slot index (for removing), or TF_ERROR (-1)
 */
bool TF_AddIdListener(TF_MSG *msg, TF_LISTENER cb, TF_TICKS timeout);

/**
 * Remove a listener by the message ID it's registered for
 *
 * @param frame_id - the frame we're listening for
 */
bool TF_RemoveIdListener(TF_ID frame_id);

/**
 * Register a frame type listener.
 *
 * @param frame_type - frame type to listen for
 * @param cb - callback
 * @return slot index (for removing), or TF_ERROR (-1)
 */
bool TF_AddTypeListener(TF_TYPE frame_type, TF_LISTENER cb);

/**
 * Remove a listener by type.
 *
 * @param type - the type it's registered for
 */
bool TF_RemoveTypeListener(TF_TYPE type);

/**
 * Register a generic listener.
 *
 * @param cb - callback
 * @return slot index (for removing), or TF_ERROR (-1)
 */
bool TF_AddGenericListener(TF_LISTENER cb);

/**
 * Remove a generic listener by function pointer
 *
 * @param cb - callback function to remove
 */
bool TF_RemoveGenericListener(TF_LISTENER cb);

/**
 * Send a frame, and optionally attach an ID listener.
 *
 * @param msg - message struct. ID is stored in the frame_id field
 * @param listener - listener waiting for the response (can be NULL)
 * @param timeout - listener expiry time in ticks
 * @return success
 */
bool TF_Send(TF_MSG *msg, TF_LISTENER listener, TF_TICKS timeout);

/**
 * Send a response to a received message.
 *
 * @param msg - message struct. ID is read from frame_id
 * @param renew - renew the listener timeout (waiting for more data)
 * @return success
 */
bool TF_Respond(TF_MSG *msg, bool renew);

/**
 * Renew ID listener timeout
 *
 * @param id - listener ID to renew
 * @return true if listener was found and renewed
 */
bool TF_RenewIdListener(TF_ID id);

/**
 * Accept incoming bytes & parse frames
 *
 * @param buffer - byte buffer to process
 * @param count - nr of bytes in the buffer
 */
void TF_Accept(const uint8_t *buffer, size_t count);

/**
 * Accept a single incoming byte
 *
 * @param c - a received char
 */
void TF_AcceptChar(uint8_t c);

/**
 * 'Write bytes' function that sends data to UART
 *
 * ! Implement this in your application code !
 */
extern void TF_WriteImpl(const uint8_t *buff, size_t len);

/**
 * This function should be called periodically.
 *
 * The time base is used to time-out partial frames in the parser and
 * automatically reset it.
 *
 * (suggestion - call this in a SysTick handler)
 */
void TF_Tick(void);

#endif
