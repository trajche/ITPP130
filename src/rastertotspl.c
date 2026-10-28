/*
 * rastertotspl - CUPS raster to TSPL filter for MUNBYN ITPP130 label printers.
 *
 * Copyright 2026 trajche and contributors.
 * Licensed under the Apache License, Version 2.0. See LICENSE.
 *
 * Emits the same TSPL command stream as the TSPL output path of MUNBYN's
 * macOS driver, so the printer can be driven from Linux CUPS.
 */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 8-bit gray pixels below this value print as black (vendor threshold). */
#define BLACK_THRESHOLD 201

static volatile sig_atomic_t canceled = 0;

static void
cancel_job(int sig)
{
  (void)sig;
  canceled = 1;
}

/* Return the marked PPD choice for keyword, or NULL if unset or "Default". */
static const char *
marked(ppd_file_t *ppd, const char *keyword)
{
  ppd_choice_t *choice = ppdFindMarkedChoice(ppd, keyword);

  if (!choice || !strcmp(choice->choice, "Default"))
    return (NULL);

  return (choice->choice);
}

static int
marked_int(ppd_file_t *ppd, const char *keyword, int def)
{
  const char *value = marked(ppd, keyword);

  return (value ? atoi(value) : def);
}

static void
start_page(ppd_file_t *ppd, cups_page_header2_t *header)
{
  const char *value;
  unsigned    width_bytes = (header->cupsWidth + 7) / 8;
  int         i;

  /* Leading NULs flush any partial command left in the printer's parser. */
  for (i = 0; i < 1024; i ++)
    putchar(0);

  /* At 203 dpi there are 8 dots per mm. */
  printf("SIZE %u mm,%u mm\r\n", width_bytes, (header->cupsHeight + 7) / 8);
  /* MUNBYN's own PPD misspells the horizontal offset keyword. */
  printf("REFERENCE %d,%d\r\n",
         marked_int(ppd, "AdjustHorizontal", marked_int(ppd, "AdjustHoriaontal", 0)) * 8,
         marked_int(ppd, "AdjustVertical", 0) * 8);
  printf("DIRECTION %d,0\r\n", marked_int(ppd, "Rotate", 0));

  if ((value = marked(ppd, "zeMediaTracking")) != NULL)
  {
    if (!strcmp(value, "Continuous"))
      printf("GAP 0 mm,0 mm\r\n");
    else
      printf(strcmp(value, "Gap") ? "BLINE %d mm,%d mm\r\n" : "GAP %d mm,%d mm\r\n",
             marked_int(ppd, "GapOrMarkHeight", 3),
             marked_int(ppd, "GapOrMarkOffset", 0));
  }

  if ((value = marked(ppd, "Darkness")) != NULL)
    printf("DENSITY %d\r\n", atoi(value));

  if ((value = marked(ppd, "zePrintRate")) != NULL)
  {
    /* The printer's minimum speed is 2 ips. */
    int speed = atoi(value);
    printf("SPEED %d\r\n", speed == 1 ? 2 : speed);
  }

  printf("SETC AUTODOTTED %s\r\n", marked_int(ppd, "AutoDotted", 0) ? "ON" : "OFF");
  printf("SETC PAUSEKEY ON\r\n");
  printf("SETC WATERMARK OFF\r\n");
  printf("CLS\r\n");
  printf("BITMAP 0,0,%u,%u,1,", width_bytes, header->cupsHeight);
}

/* Pack one 8-bit gray row into TSPL bitmap bytes, where a 1 bit is white. */
static void
output_line(cups_page_header2_t *header, const unsigned char *row)
{
  unsigned x, bit;

  for (x = 0; x < header->cupsWidth; x += 8)
  {
    unsigned char black = 0;

    for (bit = 0; bit < 8 && x + bit < header->cupsWidth; bit ++)
      if (row[x + bit] < BLACK_THRESHOLD)
        black |= 0x80 >> bit;

    putchar(~black & 0xff);
  }
}

static void
end_page(void)
{
  printf("\nPRINT 1,1\r\n");
  fflush(stdout);
}

int
main(int argc, char *argv[])
{
  int                 fd = 0;
  cups_raster_t       *ras;
  cups_page_header2_t header;
  ppd_file_t          *ppd;
  cups_option_t       *options = NULL;
  int                 num_options;
  unsigned char       *row;
  unsigned            y;
  int                 page = 0;

  if (argc < 6 || argc > 7)
  {
    fprintf(stderr, "Usage: %s job-id user title copies options [file]\n", argv[0]);
    return (1);
  }

  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0)
  {
    perror("ERROR: Unable to open raster file");
    return (1);
  }

  signal(SIGTERM, cancel_job);

  if ((ppd = ppdOpenFile(getenv("PPD"))) == NULL)
  {
    ppd_status_t status;
    int          line;

    status = ppdLastError(&line);
    fprintf(stderr, "ERROR: Unable to open PPD file: %s on line %d\n",
            ppdErrorString(status), line);
    return (1);
  }

  num_options = cupsParseOptions(argv[5], 0, &options);
  ppdMarkDefaults(ppd);
  cupsMarkOptions(ppd, num_options, options);

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

  while (!canceled && cupsRasterReadHeader2(ras, &header))
  {
    if (header.cupsBitsPerPixel != 8)
    {
      fprintf(stderr, "ERROR: Unsupported raster depth %u, expected 8\n",
              header.cupsBitsPerPixel);
      break;
    }

    page ++;
    fprintf(stderr, "PAGE: %d 1\n", page);

    if ((row = malloc(header.cupsBytesPerLine)) == NULL)
    {
      fputs("ERROR: Unable to allocate memory\n", stderr);
      break;
    }

    start_page(ppd, &header);

    for (y = 0; y < header.cupsHeight && !canceled; y ++)
    {
      if ((y & 15) == 0)
        fprintf(stderr, "ATTR: job-media-progress=%u\n", y * 100 / header.cupsHeight);

      if (cupsRasterReadPixels(ras, row, header.cupsBytesPerLine) < 1)
        break;

      output_line(&header, row);
    }

    end_page();
    free(row);
  }

  cupsRasterClose(ras);
  if (fd)
    close(fd);
  ppdClose(ppd);
  cupsFreeOptions(num_options, options);

  if (page == 0)
  {
    fputs("ERROR: No pages found\n", stderr);
    return (1);
  }

  return (0);
}
