// Author : Bojun Pan
// This is just a prototype file for the LPC 1788 ISP cmd
// It can not run but the logic for the command set are correct


//
// INCLUDED FILES
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// LOCAL DEFINITIONS, MACROS, AND TYPEDEFS
//
#define NXPRAM_FIRST512_ADDRESS (268435968)
#define NXPRAM_SECOND512_ADDRESS (268436480)
#define NXPFLASH_BEGIN_ADDRESS (0)
#define NXPFLASH_END_ADDRESS (44032)
#define FIRST_SECTOR (0)
#define MAX_SECTOR (29)

#define HANDSHANKING_START_MSG ("?")
#define HANDSHAKING_SYN_MSG ("Synchronized")
#define HANDSHAKING_ACK_MSG ("0")

#define VERSION_CHECK_CMD ("J")
#define VERSION_LEN (10)
#define PREPARE_SECTOR_CMD ("P %d %d")
#define ERASE_SECTOR_CMD ("E %d %d")
#define UNLOCK_CMD ("U 23130")
#define WRITE_FIRST_RAM_BLOCK ("W 268435968 512")
#define WRITE_SECOND_RAM_BLOCK ("W 268436480 512")
#define COPY_FROM_RAM_TO_FLASH ("C %d 268435968 1024")

#define RESPONSE_ZERO ("0")
#define RESPONSE_OK	("OK")
#define RESPONSE_SYN ("Synchronized")

#define ISP_RAM_WRITE_MAX (512)
#define ISP_FLASH_COPY_MAX (1024)

#define UART_RECV_BUFFER_SIZE (100)

#define NXP_CMD_MAX_LENGTH (64)

#define UUENCODE_MAX_BYTES (45)

#define UUENCODE_OFFSET (0x20)

#define BUFFER_SIZE (1024)

//Index into CAN data
#define START_ADDR_INDEX	0
#define SIZE_BYTES_INDEX	4
#define DATA_INDEX			8

// handshaking state machine
typedef enum {
	HANDSHAKING_START, HANDSHAKING_SYN, HANDSHAKING_ACK, HANDSHAKING_SUCCESSFUL
} HandShakingStatus_t;

// RAM buffer
static uint8_t byteBuffer[BUFFER_SIZE + 5];

// current RAM position to write from
static uint32_t curBufferSize;

// current flash address to write from
static uint32_t offset = 0;

//
// GLOBAL VARIABLE DEFINITIONS
//

//
// STATIC VARIABLE DEFINITIONS
//

//
// STATIC FUNCTION DECLARATIONS
//
void hex2uuencode(uint8_t *hexStr, uint8_t *uuencodeStr);

HandShakingStatus_t NXPDisplayHandShaking();

uint32_t NXPDisplayVersionCheck();

uint32_t NXPPrepareSectors();

uint32_t NXPDisplayCMDLength(uint8_t * cmd);

//
// START OF OPERATIONAL CODE
//


/*
 *  PARAMETERS: hexStr the previous hex src
 *  			uuencodeStr encoded uuencode format dest
 *
 *  DESCRIPTION: from hex format to uuencode, 3 bytes to 4 bytes
 *
 *  RETURNS: void
 *
 */

void hex2uuencode(uint8_t *hexStr, uint8_t *uuencodeStr) {
	int i;
	uuencodeStr[0] = (hexStr[0] >> 2) + UUENCODE_OFFSET;
	uuencodeStr[1] = ((hexStr[0] & 0x03) << 4) + (hexStr[1] >> 4)
			+ UUENCODE_OFFSET;
	uuencodeStr[2] = ((hexStr[1] & 0x0F) << 2) + (hexStr[2] >> 6)
			+ UUENCODE_OFFSET;
	uuencodeStr[3] = (hexStr[2] & 0x3F) + UUENCODE_OFFSET;
	for (i = 0; i < 4; i++) {
		if (uuencodeStr[i] == UUENCODE_OFFSET) {
			uuencodeStr[i] = 0x60;
		}
	}
}

/*
 *  PARAMETERS: response
 *
 *  DESCRIPTION: NXP handshaking and prepare sectors
 *
 *  RETURNS: void
 *
 */
void handleNXPDisplayPrepare(RspFmt_Obj *pRsp) {
	// release NXP from reset
	canIoSetPort(canREG2, 1, 1);
	uint32_t error_code = CMD_VALID;
	int i;
	for (i = 0; i < BUFFER_SIZE; i++) {
		byteBuffer[i] = 0xFF;
	}
	HandShakingStatus_t handshakingStatus;
	handshakingStatus = NXPDisplayHandShaking();

	if (handshakingStatus == HANDSHAKING_SUCCESSFUL) {
		uint32_t version = NXPDisplayVersionCheck();
		if (version != 0) {
			error_code = NXPPrepareSectors();
		} else {
			error_code = CMD_POB_REJ;
			pRsp->status = error_code;
			return;
		}
	} else {
		error_code = CMD_POB_REJ;
	}
	pRsp->status = error_code;
}

/*
 *  PARAMETERS: None
 *
 *  DESCRIPTION: NXP handshaking command
 *
 *  RETURNS: HandShakingStatus HANDSHAKING_SUCCESSFUL or not
 *
 */
HandShakingStatus_t NXPDisplayHandShaking() {
	uint8_t sendCmd[NXP_CMD_MAX_LENGTH];
	uint8_t recvBuf[LIN_RECV_BUFFER_SIZE];
	uint32_t len = 0;
	HandShakingStatus_t handShakingStatus = HANDSHAKING_START;
	switch (handShakingStatus) {
	case HANDSHAKING_START:
		snprintf((char *) sendCmd, sizeof(sendCmd), HANDSHANKING_START_MSG);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSend(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, strlen(RESPONSE_SYN));
		if (strncmp((char *) recvBuf, RESPONSE_SYN, strlen(RESPONSE_SYN))
				!= 0) {
			break;
		}
		handShakingStatus = HANDSHAKING_SYN;
	case HANDSHAKING_SYN:
		snprintf((char *) sendCmd, sizeof(sendCmd), HANDSHAKING_SYN_MSG);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, strlen(RESPONSE_SYN) + strlen(RESPONSE_OK) + 1);
		if (strncmp((char *) recvBuf, RESPONSE_SYN, strlen(RESPONSE_SYN))
				!= 0) {
			break;
		}
		if (strncmp((char *) recvBuf + strlen(RESPONSE_SYN) + 1, RESPONSE_OK,
				strlen(RESPONSE_OK)) != 0) {
			break;
		}
		handShakingStatus = HANDSHAKING_ACK;
	case HANDSHAKING_ACK:
		snprintf((char *) sendCmd, sizeof(sendCmd), HANDSHAKING_ACK_MSG);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, strlen(RESPONSE_ZERO) + strlen(RESPONSE_OK) + 1);
		if (strncmp((char *) recvBuf, RESPONSE_ZERO, strlen(RESPONSE_ZERO))
				!= 0) {
			break;
		}
		if (strncmp((char *) recvBuf + strlen(RESPONSE_ZERO) + 1, RESPONSE_OK,
				strlen(RESPONSE_OK)) != 0) {
			break;
		}
		handShakingStatus = HANDSHAKING_SUCCESSFUL;
	default:
		break;
	}
	return handShakingStatus;
}

/*
 *  PARAMETERS: Command
 *
 *  DESCRIPTION: NXP Command length
 *
 *  RETURNS: length
 *
 */
uint32_t NXPDisplayCMDLength(uint8_t * cmd) {
	return strlen((char *) cmd);
}

/*
 *  PARAMETERS: None
 *
 *  DESCRIPTION: NXP FW Version Check
 *
 *  RETURNS: Version, 0 for unsuccessful
 *
 */
uint32_t NXPDisplayVersionCheck() {
	uint32_t version = 0;
	uint8_t sendCmd[NXP_CMD_MAX_LENGTH], recvBuf[LIN_RECV_BUFFER_SIZE];
	uint32_t len = 0;
	snprintf((char *) sendCmd, sizeof(sendCmd), VERSION_CHECK_CMD);
	len = NXPDisplayCMDLength(sendCmd);
	UARTSendWithCR(sendCmd, len);
	memset(recvBuf, 0, sizeof(recvBuf));
	UARTRecv(recvBuf,
			strlen(
					VERSION_CHECK_CMD) + 1 + strlen(RESPONSE_ZERO) + 2 + VERSION_LEN);
	if (strncmp((char *) recvBuf, VERSION_CHECK_CMD, strlen(VERSION_CHECK_CMD))
			!= 0) {
		return 0;
	}
	if (strncmp((char *) recvBuf + strlen(VERSION_CHECK_CMD) + 1, RESPONSE_ZERO,
			strlen(RESPONSE_ZERO)) != 0) {
		return 0;
	}
	int i;
	for (i = strlen(VERSION_CHECK_CMD) + 1 + strlen(RESPONSE_ZERO) + 2;
			i < strlen((char *) recvBuf); i++) {
		if (recvBuf[i] >= '0' && recvBuf[i] <= '9') {
			version = version * 10 + recvBuf[i] - '0';
		}
	}
	return version;
}

/*
 *  PARAMETERS: None
 *
 *  DESCRIPTION: NXP Sector prepare and erase
 *
 *  RETURNS: Cmd Status
 *
 */
uint32_t NXPPrepareSectors() {
	uint8_t sendCmd[NXP_CMD_MAX_LENGTH], recvBuf[LIN_RECV_BUFFER_SIZE];
	uint32_t len = 0;

	int i;
	for (i = 0; i < BUFFER_SIZE; i++) {
		byteBuffer[i] = 0xFF;
	}

	// U command unlock the flash write/eraze
	snprintf((char *) sendCmd, sizeof(sendCmd), UNLOCK_CMD);
	len = NXPDisplayCMDLength(sendCmd);
	UARTSendWithCR(sendCmd, len);
	memset(recvBuf, 0, sizeof(recvBuf));
	UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
	if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
		return CMD_POB_REJ;
	}
	if (strncmp((char *) recvBuf + 1 + len, RESPONSE_ZERO,
			strlen(RESPONSE_ZERO)) != 0) {
		return CMD_POB_REJ;
	}

	// P command prepare the flash sector
	int firstSector = FIRST_SECTOR;
	int lastSector = MAX_SECTOR;
	snprintf((char *) sendCmd, sizeof(sendCmd), PREPARE_SECTOR_CMD, firstSector,
			lastSector);
	len = NXPDisplayCMDLength(sendCmd);
	UARTSendWithCR(sendCmd, len);
	memset(recvBuf, 0, sizeof(recvBuf));
	UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
	if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
		return CMD_POB_REJ;
	}
	if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
			strlen(RESPONSE_ZERO)) != 0) {
		return CMD_POB_REJ;
	}

	// E command eraze the sectors
	snprintf((char *) sendCmd, sizeof(sendCmd), ERASE_SECTOR_CMD, firstSector,
			lastSector);
	len = NXPDisplayCMDLength(sendCmd);
	UARTSendWithCR(sendCmd, len);
	memset(recvBuf, 0, sizeof(recvBuf));
	UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
	if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
		return CMD_POB_REJ;
	}
	if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
			strlen(RESPONSE_ZERO)) != 0) {
		return CMD_POB_REJ;
	}

	return CMD_VALID;
}


/*
 *  PARAMETERS: Command, response
 *
 *  DESCRIPTION: NXP Write data Commands
 *
 *  RETURNS: void
 *
 */
void handleNXPDisplayWrite(uint8_t *pCmd, RspFmt_Obj *pRsp) {
	//uint32_t	startAddr;
	uint32_t sizeInBytes;
	uint32_t bytesRemain;
	uint32_t aa;
	uint8_t *pData;
	uint32_t newData[NUM_PARAMS_MAX];

	// misc init
	//startAddr = (pCmd[START_ADDR_INDEX] << 24) | (pCmd[START_ADDR_INDEX + 1] << 16) | (pCmd[START_ADDR_INDEX + 2] << 8) | pCmd[START_ADDR_INDEX + 3];

	sizeInBytes = (pCmd[SIZE_BYTES_INDEX] << 24)
			| (pCmd[SIZE_BYTES_INDEX + 1] << 16)
			| (pCmd[SIZE_BYTES_INDEX + 2] << 8) | pCmd[SIZE_BYTES_INDEX + 3];
	pData = (uint8_t *) &pCmd[DATA_INDEX];
	//bytesRemain = sizeInBytes;

	// TODO: BUGS!!! It's strange that the raw data will be different with the line of the code below, it doesn't actually do anything...
	// Need to figure out why...
	aa = sizeInBytes;

	int paramIndex;

	//Byte swap the s record data.  Since the DSP is LE and the SMB is BE,
	//the SMB byte swaps messgaes when they are received.  However, the s record
	//is sent in the correct order, so we have to byte swap it again to put it
	//back in the correct order.
	for (paramIndex = 0; paramIndex < (sizeInBytes / 4); paramIndex++) {
		newData[paramIndex] = (((uint32_t) pData[(4 * paramIndex) + 3] << 24)
				+ ((uint32_t) pData[(4 * paramIndex) + 2] << 16)
				+ ((uint32_t) pData[(4 * paramIndex) + 1] << 8)
				+ ((uint32_t) pData[(4 * paramIndex) + 0]));
	}

	pData = (uint8_t *) &newData;
	if (curBufferSize + sizeInBytes < BUFFER_SIZE) {
		memcpy(byteBuffer + curBufferSize, pData, sizeInBytes);
		curBufferSize += sizeInBytes;
	} else {
		// fit exactly 1024 data
		bytesRemain = BUFFER_SIZE - curBufferSize;
		memcpy(byteBuffer + curBufferSize, pData, bytesRemain);

		// play tricks with Checksum
		uint32_t chksum = 0;
		int i;
		int j;
		/*if (offset == 0) {
			for (i = 0; i < 0x1C; i += 4) {
				chksum += *(uint8_t *) (byteBuffer + i);
			}
			*(uint8_t *) (byteBuffer + 0x1C) = 0xFFFFFFFF - chksum + 1;
		}*/
		chksum = 0;

		for (i = 0; i < ISP_RAM_WRITE_MAX; i++) {
			chksum += byteBuffer[i];
		}

		uint8_t sendCmd[NXP_CMD_MAX_LENGTH], recvBuf[LIN_RECV_BUFFER_SIZE];
		uint32_t len = 0;

		// U command unlock the flash write/eraze
		snprintf((char *) sendCmd, sizeof(sendCmd), UNLOCK_CMD);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		//write to RAM address 10000200h, 512 bytes
		snprintf((char *) sendCmd, sizeof(sendCmd), WRITE_FIRST_RAM_BLOCK);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + 1 + len, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		int num;
		// uuencode of the first 512 bytes data
		for (i = 0; i < ISP_RAM_WRITE_MAX; i += UUENCODE_MAX_BYTES) { // max 45 bytes a time
			num = ISP_RAM_WRITE_MAX - i;
			if (num > UUENCODE_MAX_BYTES) {
				num = UUENCODE_MAX_BYTES;
			}
			sendCmd[0] = num + UUENCODE_OFFSET;
			for (j = 0; j < num; j += 3)
				hex2uuencode(byteBuffer + i + j, sendCmd + 1 + (j / 3) * 4);
			sendCmd[1 + ((num + 2) / 3) * 4] = 0;
			len = NXPDisplayCMDLength(sendCmd);
			UARTSendWithCR(sendCmd, len);
			memset(recvBuf, 0, sizeof(recvBuf));
			UARTRecv(recvBuf, len);
			if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
				return;
			}
		}

		// check-sum
		snprintf((char *) sendCmd, sizeof(sendCmd), "%d", chksum);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + strlen(RESPONSE_OK) + 1);
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_OK,
				strlen(RESPONSE_OK)) != 0) {
			return;
		}

		// Write to RAM address 10000400h, 512 bytes
		snprintf((char *) sendCmd, sizeof(sendCmd), WRITE_SECOND_RAM_BLOCK);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + strlen(RESPONSE_ZERO) + 1);
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		chksum = 0;
		for (i = 0; i < 512; i++) {
			chksum += byteBuffer[i + 512];
		}

		// uuencode of the second 512 bytes data
		for (i = 0; i < ISP_RAM_WRITE_MAX; i += UUENCODE_MAX_BYTES) { // max 45 bytes a time
			num = ISP_RAM_WRITE_MAX - i;
			if (num > UUENCODE_MAX_BYTES) {
				num = UUENCODE_MAX_BYTES;
			}
			sendCmd[0] = num + UUENCODE_OFFSET;
			for (j = 0; j < num; j += 3)
				hex2uuencode(byteBuffer + ISP_RAM_WRITE_MAX + i + j,
						sendCmd + 1 + (j / 3) * 4);
			sendCmd[1 + ((num + 2) / 3) * 4] = 0;
			len = NXPDisplayCMDLength(sendCmd);
			UARTSendWithCR(sendCmd, len);
			memset(recvBuf, 0, sizeof(recvBuf));
			UARTRecv(recvBuf, len);
			if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
				return;
			}
		}

		// check-sum
		snprintf((char *) sendCmd, sizeof(sendCmd), "%d", chksum);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_OK));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_OK,
				strlen(RESPONSE_OK)) != 0) {
			return;
		}

		// P command
		int firstSector = FIRST_SECTOR;
		int lastSector = MAX_SECTOR;
		snprintf((char *) sendCmd, sizeof(sendCmd), PREPARE_SECTOR_CMD,
				firstSector, lastSector);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		// copy to flash address (offset) from RAM address 10000200h, 1024 bytes
		snprintf((char *) sendCmd, sizeof(sendCmd), COPY_FROM_RAM_TO_FLASH,
				offset);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd,
				strlen((char *) sendCmd)) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		// put the remained data to the byteBuffer
		curBufferSize = 0;
		for (i = 0; i < BUFFER_SIZE; i++) {
			byteBuffer[i] = 0xFF;
		}
		memcpy(byteBuffer + curBufferSize, pData + bytesRemain,
				sizeInBytes - bytesRemain);
		offset += 1024;
	}

	pRsp->status = CMD_VALID;
}

/*
 *  PARAMETERS: response
 *
 *  DESCRIPTION: NXP write last block of data and terminate
 *
 *  RETURNS: void
 *
 */
void handleNXPDisplayTerminate(RspFmt_Obj *pRsp) {
	int i;
	int j;
	if (curBufferSize != 0) {
		uint32_t chksum = 0;
		chksum = 0;
		for (i = 0; i < ISP_RAM_WRITE_MAX; i++) {
			chksum += byteBuffer[i];
		}

		uint8_t sendCmd[NXP_CMD_MAX_LENGTH], recvBuf[LIN_RECV_BUFFER_SIZE];
		uint32_t len = 0;

		// U command unlock the flash write/eraze
		snprintf((char *) sendCmd, sizeof(sendCmd), UNLOCK_CMD);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		//write to RAM address 10000200h, 512 bytes
		snprintf((char *) sendCmd, sizeof(sendCmd), WRITE_FIRST_RAM_BLOCK);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + 1 + len, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		int num;
		// uuencode of the first 512 bytes data
		for (i = 0; i < ISP_RAM_WRITE_MAX; i += UUENCODE_MAX_BYTES) { // max 45 bytes a time
			num = ISP_RAM_WRITE_MAX - i;
			if (num > UUENCODE_MAX_BYTES) {
				num = UUENCODE_MAX_BYTES;
			}
			sendCmd[0] = num + UUENCODE_OFFSET;
			for (j = 0; j < num; j += 3)
				hex2uuencode(byteBuffer + i + j, sendCmd + 1 + (j / 3) * 4);
			sendCmd[1 + ((num + 2) / 3) * 4] = 0;
			len = NXPDisplayCMDLength(sendCmd);
			UARTSendWithCR(sendCmd, len);
			memset(recvBuf, 0, sizeof(recvBuf));
			UARTRecv(recvBuf, len);
			if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
				return;
			}
		}

		// check-sum
		snprintf((char *) sendCmd, sizeof(sendCmd), "%d", chksum);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + strlen(RESPONSE_OK) + 1);
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_OK,
				strlen(RESPONSE_OK)) != 0) {
			return;
		}

		chksum = 0;
		for (i = 0; i < 512; i++) {
			chksum += byteBuffer[i + 512];
		}

		// Write to RAM address 10000400h, 512 bytes
		snprintf((char *) sendCmd, sizeof(sendCmd), WRITE_SECOND_RAM_BLOCK);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + strlen(RESPONSE_ZERO) + 1);
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		// uuencode of the second 512 bytes data
		for (i = 0; i < ISP_RAM_WRITE_MAX; i += UUENCODE_MAX_BYTES) { // max 45 bytes a time
			num = ISP_RAM_WRITE_MAX - i;
			if (num > UUENCODE_MAX_BYTES) {
				num = UUENCODE_MAX_BYTES;
			}
			sendCmd[0] = num + UUENCODE_OFFSET;
			for (j = 0; j < num; j += 3)
				hex2uuencode(byteBuffer + ISP_RAM_WRITE_MAX + i + j,
						sendCmd + 1 + (j / 3) * 4);
			sendCmd[1 + ((num + 2) / 3) * 4] = 0;
			len = NXPDisplayCMDLength(sendCmd);
			UARTSendWithCR(sendCmd, len);
			memset(recvBuf, 0, sizeof(recvBuf));
			UARTRecv(recvBuf, len);
			if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
				return;
			}
		}

		// check-sum
		snprintf((char *) sendCmd, sizeof(sendCmd), "%d", chksum);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_OK));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_OK,
				strlen(RESPONSE_OK)) != 0) {
			return;
		}

		// P command
		int firstSector = FIRST_SECTOR;
		int lastSector = MAX_SECTOR;
		snprintf((char *) sendCmd, sizeof(sendCmd), PREPARE_SECTOR_CMD,
				firstSector, lastSector);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd, len) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}

		// copy to flash address (offset) from RAM address 10000200h, 1024 bytes
		snprintf((char *) sendCmd, sizeof(sendCmd), COPY_FROM_RAM_TO_FLASH,
				offset);
		len = NXPDisplayCMDLength(sendCmd);
		UARTSendWithCR(sendCmd, len);
		memset(recvBuf, 0, sizeof(recvBuf));
		UARTRecv(recvBuf, len + 1 + strlen(RESPONSE_ZERO));
		if (strncmp((char *) recvBuf, (char *) sendCmd,
				strlen((char *) sendCmd)) != 0) {
			return;
		}
		if (strncmp((char *) recvBuf + len + 1, RESPONSE_ZERO,
				strlen(RESPONSE_ZERO)) != 0) {
			return;
		}
	}
	pRsp->status = CMD_VALID;
}
