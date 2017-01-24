/*
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 * Copyright (C) 2017, Boundary Devices <info@boundarydevices.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 	* Redistributions of source code must retain the above copyright
 * 	  notice, this list of conditions and the following disclaimer.
 *
 * 	* Redistributions in binary form must reproduce the above copyright
 * 	  notice, this list of conditions and the following disclaimer in the
 * 	  documentation and/or other materials provided with the
 * 	  distribution.
 *
 * 	* Neither the name of Texas Instruments Incorporated nor the names of
 * 	  its contributors may be used to endorse or promote products derived
 * 	  from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Thermal Printer Demo Application
 *
 * Written by Andreas Dannenberg, 01/01/2014
 * Modified for Processor-SDK by Michael Snook, 07/21/2015
 * Modified by Boundary Devices for NXP i.MX demo 20/01/2017
 */

#include <png.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

/* Commands to be used to stick into the 'PRINTER_JobItem.command' fields of the */
#define PRINTER_CMD_STANDBY				0x00
#define PRINTER_CMD_PRINT_LINE				0x01
#define PRINTER_CMD_ADVANCE_LINE			0x02
#define PRINTER_CMD_CUT_PAPER				0x03
#define PRINTER_CMD_HALT_ALL				0xFF

/* Define how long an actual printed line is */
#define PRINTER_DOTS_PER_LINE				384
#define PRINTER_BYTES_PER_LINE				(PRINTER_DOTS_PER_LINE / 8)

/*
 * Type containing a single job item. A print job consists of a series of job
 * items. A job item's command field comprises the specific action to take.
 * the length field denotes how much payload data is associated with a the
 * command, and data is a variable-length array containing the actual payload
 * data. Note that when length is zero no payload data is contained in the job
 * item in which case the next job item will start right where the first data
 * element of the previous job item would have been. If length is one then
 * there is one 32-bit word of data, and so on.
 */
typedef struct {
	uint32_t command;
	uint32_t length;
	uint8_t data[];
} PRINTER_JobItem;

/* Global variables for working with the image that was loaded */
static uint32_t pngImageWidth, pngImageHeight;
static png_bytep *pngImageRowPointers;

/* Function prototypes */
static bool sendJob(PRINTER_JobItem *currentItem);
static bool printImage(void);
static bool readPngImage(const char *fileName);
static void deallocPngImage(void);
static void PrintLine(const png_bytep * rowPtr);

/* Main Linux program entry point */
int main(int argc, char * const argv[])
{
	char opt;

	/*
	 * Parse the command line options and issue a simple error text in case
	 * things don't match up. The colon behind the options denote that option
	 * requires an argument. For now we only have one option: -i. Should be
	 * expanded for additional functionallity.
	 */
	opt = getopt(argc, argv, "i:");
	if(opt == 'i') {
		/* PNG image filename is stored in argv[2] */
		printf("Printing image: %s\n", argv[2]);
	}
	/* error condition */
	else {
		printf("usage: %s [-i <image.png>]\n", argv[0]);

		return EXIT_FAILURE;
	}
	readPngImage(argv[2]);
	printImage();
	printf("Image printed\n");
	deallocPngImage();
	return EXIT_SUCCESS;
}

/*
 * Sends 1 line of data (64 Bytes), 384 dots (bits).
 */
static bool sendJob(PRINTER_JobItem * currentItem) {
	FILE *ofp;
	char outputFilename[] = "/dev/ftp628";

	ofp = fopen(outputFilename, "w");
	if (ofp == NULL) {
		printf("ERROR: couldn't open device");
		exit(EXIT_FAILURE);
	}

	if (currentItem->command == PRINTER_CMD_ADVANCE_LINE) {
		char buf = '\r';
		fwrite(&buf, sizeof(uint8_t), sizeof(buf), ofp);
	} else {
		fwrite(currentItem->data, sizeof(uint8_t), currentItem->length, ofp);
	}

	fclose(ofp);
	return true;
}

/*
 * Function that handles the creation of commands, printer job items and
 * sends them to device.
 */
static bool printImage(void) {
	png_bytep * rowPtr;
	int lineCount;
	int i;

	PRINTER_JobItem * advancePaperItem;
	advancePaperItem = (PRINTER_JobItem *) malloc(sizeof(PRINTER_JobItem) + 0);
	advancePaperItem->command = PRINTER_CMD_ADVANCE_LINE;
	advancePaperItem->length = 0;

	/*
	 * We are going to step through and process the image and associated
	 * print jobs/commands one line (row) at a time. We'll use rowPtr to
	 * keep track of what row we are working with. Once the loop has
	 * ended, we know we are done and ready to cut the paper.
	 */
	for(lineCount = 0; lineCount < pngImageHeight; lineCount++) {
		rowPtr = (png_bytep *)pngImageRowPointers[lineCount];

		/*
		 * print line on two separate lines, effectively printing each dot
		 * twice. This increases contrast and fixes aspect ratio issues.
		 */
		PrintLine(rowPtr);
		sendJob(advancePaperItem);
		PrintLine(rowPtr);
		sendJob(advancePaperItem);
	}

	printf("Advance paper\n");
	for(i = 0; i < 100; i++) {
		sendJob(advancePaperItem);
	}

	return true;
}

static bool readPngImage(const char *fileName) {
	FILE *fp;
	unsigned char pngSignature[8];      // The PNG signature is 8 bytes long
	png_byte bitDepth;
	png_structp png_ptr;
	png_infop info_ptr;
	uint32_t y;

	/* Open image file */
	fp = fopen(fileName, "rb");
	if (!fp) {
		printf("File could not be opened for reading!\n");
		return false;
	}

	/* Test image file for being a PNG by evaluating its header */
	fread(pngSignature, 1, sizeof(pngSignature), fp);
	if (png_sig_cmp(pngSignature, 0, sizeof(pngSignature))) {
		printf("File not recognized as a PNG file!\n");
		return false;
	}

	/* Initialize libpng in preparation for reading the image */
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr) {
		printf("Error during during PNG initialization!\n");
		return false;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		printf("Error during during PNG initialization!\n");
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		printf("Error during during PNG initialization!\n");
		return false;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);

	/* Read important image parameters and store them in local/global variables */
	pngImageWidth = png_get_image_width(png_ptr, info_ptr);
	pngImageHeight = png_get_image_height(png_ptr, info_ptr);
	bitDepth = png_get_bit_depth(png_ptr, info_ptr);

	printf("Image width = %u\n", pngImageWidth);
	printf("Image height = %u\n", pngImageHeight);

	/* TODO Error out images larger in 384 bits wide. */
	if (bitDepth != 1) {
		printf("Only monochrome images (1-bit) are allowed! Provided" \
		       " image is %u bits deep.\n", bitDepth);
		return false;
	}

	/*
	 * Enable the interlace handling and updates the structure pointed to by
	 * info_ptr to reflect any transformations that have been requested.
	 */
	png_set_interlace_handling(png_ptr);
	png_read_update_info(png_ptr, info_ptr);

	/*
	 * Allocate memory to hold an array of pointers that point to the respective
	 * row image data
	 */
	if (pngImageRowPointers) {
		printf("Error allocating memory for image. Was the last " \
		       "image loaded deallocated properly?\n");
		return false;
	}
	pngImageRowPointers = (png_bytep *)malloc(sizeof(png_bytep) * pngImageHeight);
	if (!pngImageRowPointers) {
		printf("Error allocating memory for image!\n");
		return false;
	}

	/*
	 * Allocate an individual block of memory for each row of the image. Note that
	 * this function assumes a 1 bit-per-pixel image. If an image is to be read
	 * with greater color depth the amount of memory that is allocated needs to
	 * be increased.
	 */
	for (y = 0; y < pngImageHeight; y++) {
		if (pngImageRowPointers[y]) {
			printf("Error allocating memory for image. Was the last " \
			       "image loaded deallocated properly?\n");
			return false;
		}
		pngImageRowPointers[y] = (png_byte *)malloc(48);
		if (!pngImageRowPointers[y]) {
			printf("Error allocating memory for image!\n");
			return false;
		}
	}

	/* Establish an error handler for issues during the upcoming file operation */
	if (setjmp(png_jmpbuf(png_ptr))) {
		printf("Error during png_read_image!\n");
		return false;
	}

	/* Read the entire PNG image into memory */
	png_read_image(png_ptr, pngImageRowPointers);

	fclose(fp);

	printf("Image loaded successfully\n");

	return true;
}

static void deallocPngImage(void) {
	uint32_t y;

	/* Free the memory used for each line of image data */
	for (y = 0; y < pngImageHeight; y++) {
		free(pngImageRowPointers[y]);
		pngImageRowPointers[y] = NULL;
	}

	/* Free the memory used for the array that holds the row pointers */
	free(pngImageRowPointers);
	pngImageRowPointers = NULL;
}

static void PrintLine(const png_bytep * rowPtr) {
	PRINTER_JobItem * printItem;
	printItem = (PRINTER_JobItem *) malloc(sizeof(PRINTER_JobItem) + pngImageWidth / 8 * sizeof(uint8_t));
	printItem->command = PRINTER_CMD_PRINT_LINE;
	printItem->length = pngImageWidth / 8;
	/* Tricky working with flexible array member at end of struct, need to use memcpy.*/
	memcpy(printItem->data, rowPtr, pngImageWidth / 8);
	sendJob(printItem);
}
