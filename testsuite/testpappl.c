//
// Main test suite file for the Printer Application Framework
//
// Copyright © 2020-2021 by Michael R Sweet.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Usage:
//
//   testpappl [OPTIONS] ["SERVER NAME"]
//
// Options:
//
//   --help               Show help
//   --list[-TYPE]        List devices (dns-sd, local, network, usb)
//   --no-tls             Don't support TLS
//   --version            Show version
//   -1                   Single queue
//   -A PAM-SERVICE       Enable authentication using PAM service
//   -c                   Do a clean run (no loading of state)
//   -d SPOOL-DIRECTORY   Set the spool directory
//   -l LOG-FILE          Set the log file
//   -L LOG-LEVEL         Set the log level (fatal, error, warn, info, debug)
//   -m DRIVER-NAME       Add a printer with the named driver
//   -p PORT              Set the listen port (default auto)
//   -t TEST-NAME         Run the named test (see below)
//   -T                   Enable TLS-only mode
//   -U                   Enable USB printer gadget
//
// Tests:
//
//   all                  All of the following tests
//   api                  API tests
//   client               Simulated client tests
//   jpeg                 JPEG image tests
//   png                  PNG image tests
//   pwg-raster           PWG Raster tests
//

//
// Include necessary headers...
//

#include <pappl/base-private.h>
#include <cups/dir.h>
#include "testpappl.h"
#include <stdlib.h>
#include <limits.h>

#ifdef HAVE_ARC4RANDOM
#  define TESTRAND arc4random()
#else
#  define TESTRAND random()
#endif // HAVE_ARC4RANDOM


//
// Local types...
//

typedef struct _pappl_testdata_s	// Test data
{
  cups_array_t		*names;		// Tests to run
  pappl_system_t	*system;	// System
  const char		*outdirname;	// Output directory
  bool			waitsystem;	// Wait for system to start?
} _pappl_testdata_t;


//
// Local functions...
//

static http_t	*connect_to_printer(pappl_system_t *system, char *uri, size_t urisize);
static void	device_error_cb(const char *message, void *err_data);
static bool	device_list_cb(const char *device_info, const char *device_uri, const char *device_id, void *data);
static const char *make_raster_file(ipp_t *response, bool grayscale, char *tempname, size_t tempsize);
static void	*run_tests(_pappl_testdata_t *testdata);
static bool	test_api(pappl_system_t *system);
static bool	test_api_printer(pappl_printer_t *printer);
static bool	test_client(pappl_system_t *system);
#if defined(HAVE_LIBJPEG) || defined(HAVE_LIBPNG)
static bool	test_image_files(pappl_system_t *system, const char *prompt, const char *format, int num_files, const char * const *files);
#endif // HAVE_LIBJPEG || HAVE_LIBPNG
static bool	test_pwg_raster(pappl_system_t *system);
static int	usage(int status);


//
// 'main()' - Main entry for test suite.
//

int
main(int  argc,				// I - Number of command-line arguments
     char *argv[])			// I - Command-line arguments
{
  int			i;		// Looping var
  const char		*opt,		// Current option
			*name = NULL,	// System name, if any
			*spool = NULL,	// Spool directory, if any
			*outdir = ".",	// Output directory
			*log = NULL,	// Log file, if any
			*auth = NULL,	// Auth service, if any
			*model;		// Current printer model
  cups_array_t		*models;	// Printer models, if any
  int			port = 0;	// Port number, if any
  pappl_loglevel_t	level = PAPPL_LOGLEVEL_DEBUG;
  					// Log level
  bool			clean = false,	// Clean run?
			tls_only = false;
					// Restrict to TLS only?
  char			outdirname[PATH_MAX],
					// Output directory name
			device_uri[1024];
					// Device URI for printers
  pappl_soptions_t	soptions = PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE | PAPPL_SOPTIONS_WEB_LOG | PAPPL_SOPTIONS_WEB_NETWORK | PAPPL_SOPTIONS_WEB_SECURITY | PAPPL_SOPTIONS_WEB_TLS | PAPPL_SOPTIONS_RAW_SOCKET;
					// System options
  pappl_system_t	*system;	// System
  pappl_printer_t	*printer;	// Printer
  _pappl_testdata_t	testdata;	// Test data
  pthread_t		testid = 0;	// Test thread ID
  static pappl_contact_t contact =	// Contact information
  {
    "Michael R Sweet",
    "msweet@example.org",
    "+1-705-555-1212"
  };
  static pappl_version_t versions[1] =	// Software versions
  {
    { "Test System", "", "1.0 build 42", { 1, 0, 0, 42 } }
  };


  // Parse command-line options...
  models         = cupsArrayNew(NULL, NULL);
  testdata.names = cupsArrayNew(NULL, NULL);

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
    {
      return (usage(0));
    }
    else if (!strcmp(argv[i], "--list"))
    {
      papplDeviceList(PAPPL_DEVTYPE_ALL, device_list_cb, NULL, device_error_cb, NULL);
      return (0);
    }
    else if (!strcmp(argv[i], "--list-dns-sd"))
    {
      papplDeviceList(PAPPL_DEVTYPE_DNS_SD, device_list_cb, NULL, device_error_cb, NULL);
      return (0);
    }
    else if (!strcmp(argv[i], "--list-local"))
    {
      papplDeviceList(PAPPL_DEVTYPE_LOCAL, device_list_cb, NULL, device_error_cb, NULL);
      return (0);
    }
    else if (!strcmp(argv[i], "--list-network"))
    {
      papplDeviceList(PAPPL_DEVTYPE_NETWORK, device_list_cb, NULL, device_error_cb, NULL);
      return (0);
    }
    else if (!strcmp(argv[i], "--list-usb"))
    {
      papplDeviceList(PAPPL_DEVTYPE_USB, device_list_cb, NULL, device_error_cb, NULL);
      return (0);
    }
    else if (!strcmp(argv[i], "--no-tls"))
    {
      soptions |= PAPPL_SOPTIONS_NO_TLS;
    }
    else if (!strcmp(argv[i], "--version"))
    {
      puts(PAPPL_VERSION);
      return (0);
    }
    else if (!strncmp(argv[i], "--", 2))
    {
      printf("testpappl: Unknown option '%s'.\n", argv[i]);
      return (usage(1));
    }
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
        switch (*opt)
        {
          case '1' : // -1 (single queue)
              soptions &= (pappl_soptions_t)~PAPPL_SOPTIONS_MULTI_QUEUE;
              break;
          case 'A' : // -A PAM-SERVICE
              i ++;
              if (i >= argc)
              {
                puts("testpappl: Expected PAM service name after '-A'.");
                return (usage(1));
	      }
	      auth = argv[i];
              break;
          case 'c' : // -c (clean run)
              clean = true;
              break;
          case 'd' : // -d SPOOL-DIRECTORY
              i ++;
              if (i >= argc)
              {
                puts("testpappl: Expected spool directory after '-d'.");
                return (usage(1));
	      }
	      spool = argv[i];
              break;
          case 'l' : // -l LOG-FILE
              i ++;
              if (i >= argc)
              {
                puts("testpappl: Expected log file after '-l'.");
                return (usage(1));
	      }
	      log = argv[i];
              break;
          case 'L' : // -L LOG-LEVEL
              i ++;
              if (i >= argc)
              {
                puts("testpappl: Expected log level after '-L'.");
                return (usage(1));
	      }

              if (!strcmp(argv[i], "fatal"))
	      {
                level = PAPPL_LOGLEVEL_FATAL;
	      }
	      else if (!strcmp(argv[i], "error"))
	      {
                level = PAPPL_LOGLEVEL_ERROR;
	      }
	      else if (!strcmp(argv[i], "warn"))
	      {
                level = PAPPL_LOGLEVEL_WARN;
	      }
	      else if (!strcmp(argv[i], "info"))
	      {
                level = PAPPL_LOGLEVEL_INFO;
	      }
	      else if (!strcmp(argv[i], "debug"))
	      {
                level = PAPPL_LOGLEVEL_DEBUG;
	      }
	      else
	      {
	        printf("testpappl: Unknown log level '%s'.\n", argv[i]);
	        return (usage(1));
	      }
              break;
	  case 'm' : // -m DRIVER-NAME
	      i ++;
              if (i >= argc)
              {
                puts("testpappl: Expected driver name after '-m'.");
                return (usage(1));
	      }
	      cupsArrayAdd(models, argv[i]);
              break;
	  case 'o' : // -o OUTPUT-DIRECTORY
	      i ++;
	      if (i >= argc)
	      {
                puts("testpappl: Expected output directory after '-o'.");
                return (usage(1));
	      }
	      outdir = argv[i];
	      break;
          case 'p' : // -p PORT-NUMBER
              i ++;
              if (i >= argc || atoi(argv[i]) <= 0 || atoi(argv[i]) > 32767)
              {
                puts("testpappl: Expected port number after '-p'.");
                return (usage(1));
	      }
	      port = atoi(argv[i]);
              break;
	  case 't' : // -t TEST
	      i ++;
	      if (i >= argc)
	      {
                puts("testpappl: Expected test name after '-t'.");
                return (usage(1));
	      }

	      if (!strcmp(argv[i], "all"))
	      {
		cupsArrayAdd(testdata.names, "api");
		cupsArrayAdd(testdata.names, "client");
		cupsArrayAdd(testdata.names, "jpeg");
		cupsArrayAdd(testdata.names, "png");
		cupsArrayAdd(testdata.names, "pwg-raster");
	      }
	      else
	      {
		cupsArrayAdd(testdata.names, argv[i]);
	      }
	      break;
	  case 'T' : // -T (TLS only)
	      tls_only = true;
	      break;
	  case 'U' : // -U (USB printer gadget)
	      soptions |= PAPPL_SOPTIONS_USB_PRINTER;
	      break;
	  default :
	      printf("testpappl: Unknown option '-%c'.\n", *opt);
	      return (usage(1));
        }
      }
    }
    else if (name)
    {
      printf("testpappl: Unexpected argument '%s'.\n", argv[i]);
      return (usage(1));
    }
    else
    {
      // "SERVER NAME"
      name = argv[i];
    }
  }

  // Initialize the system and any printers...
  system = papplSystemCreate(soptions, name ? name : "Test System", port, "_print,_universal", spool, log, level, auth, tls_only);
  papplSystemAddListeners(system, NULL);
  papplSystemSetPrinterDrivers(system, (int)(sizeof(pwg_drivers) / sizeof(pwg_drivers[0])), pwg_drivers, pwg_autoadd, /* create_cb */NULL, pwg_callback, "testpappl");
  papplSystemAddLink(system, "Configuration", "/config", true);
  papplSystemSetFooterHTML(system,
                           "Copyright &copy; 2020-2021 by Michael R Sweet. "
                           "Provided under the terms of the <a href=\"https://www.apache.org/licenses/LICENSE-2.0\">Apache License 2.0</a>.");
  papplSystemSetSaveCallback(system, (pappl_save_cb_t)papplSystemSaveState, (void *)"testpappl.state");
  papplSystemSetVersions(system, (int)(sizeof(versions) / sizeof(versions[0])), versions);

  httpAssembleURIf(HTTP_URI_CODING_ALL, device_uri, sizeof(device_uri), "file", NULL, NULL, 0, "%s?ext=pwg", realpath(outdir, outdirname));

  if (clean || !papplSystemLoadState(system, "testpappl.state"))
  {
    papplSystemSetContact(system, &contact);
    papplSystemSetDNSSDName(system, name ? name : "Test System");
    papplSystemSetGeoLocation(system, "geo:46.4707,-80.9961");
    papplSystemSetLocation(system, "Test Lab 42");
    papplSystemSetOrganization(system, "Lakeside Robotics");

    if (cupsArrayCount(models))
    {
      for (model = (const char *)cupsArrayFirst(models), i = 1; model; model = (const char *)cupsArrayNext(models), i ++)
      {
        char	pname[128];		// Printer name

        if (cupsArrayCount(models) == 1)
	  snprintf(pname, sizeof(pname), "%s", name ? name : "Test Printer");
        else
	  snprintf(pname, sizeof(pname), "%s %d", name ? name : "Test Printer", i);

	printer = papplPrinterCreate(system, /* printer_id */0, pname, model, "MFG:PWG;MDL:Test Printer;", device_uri);
	papplPrinterSetContact(printer, &contact);
	papplPrinterSetDNSSDName(printer, pname);
	papplPrinterSetGeoLocation(printer, "geo:46.4707,-80.9961");
	papplPrinterSetLocation(printer, "Test Lab 42");
	papplPrinterSetOrganization(printer, "Lakeside Robotics");
      }
    }
    else
    {
      printer = papplPrinterCreate(system, /* printer_id */0, "Office Printer", "pwg_common-300dpi-600dpi-srgb_8", "MFG:PWG;MDL:Office Printer;", device_uri);
      papplPrinterSetContact(printer, &contact);
      papplPrinterSetDNSSDName(printer, "Office Printer");
      papplPrinterSetGeoLocation(printer, "geo:46.4707,-80.9961");
      papplPrinterSetLocation(printer, "Test Lab 42");
      papplPrinterSetOrganization(printer, "Lakeside Robotics");

      if (soptions & PAPPL_SOPTIONS_MULTI_QUEUE)
      {
	printer = papplPrinterCreate(system, /* printer_id */0, "Label Printer", "pwg_4inch-203dpi-black_1", "MFG:PWG;MDL:Label Printer;", device_uri);
	papplPrinterSetContact(printer, &contact);
	papplPrinterSetDNSSDName(printer, "Label Printer");
	papplPrinterSetGeoLocation(printer, "geo:46.4707,-80.9961");
	papplPrinterSetLocation(printer, "Test Lab 42");
	papplPrinterSetOrganization(printer, "Lakeside Robotics");
      }
    }
  }

  cupsArrayDelete(models);

  // Run any test(s)...
  if (cupsArrayCount(testdata.names))
  {
    testdata.outdirname = outdirname;
    testdata.system     = system;

    if (cupsArrayCount(testdata.names) == 1 && !strcmp((char *)cupsArrayFirst(testdata.names), "api"))
    {
      // Running API test alone does not start system...
      testdata.waitsystem = false;
      return (run_tests(&testdata) != NULL);
    }

    testdata.waitsystem = true;

    if (pthread_create(&testid, NULL, (void *(*)(void *))run_tests, &testdata))
    {
      perror("Unable to start testing thread");
      return (1);
    }
  }

  // Run the system...
  papplSystemRun(system);

  if (testid)
  {
    void *ret;				// Return value from testing thread

    if (pthread_join(testid, &ret))
    {
      perror("Unable to get testing thread status");
      return (1);
    }
    else
      return (ret != NULL);
  }

  return (0);
}


//
// 'connect_to_printer()' - Connect to the system and return the printer URI.
//

static http_t *				// O - HTTP connection
connect_to_printer(
    pappl_system_t *system,		// I - System
    char           *uri,		// I - URI buffer
    size_t         urisize)		// I - Size of URI buffer
{
  httpAssembleURI(HTTP_URI_CODING_ALL, uri, (int)urisize, "ipp", NULL, "localhost", papplSystemGetPort(system), "/ipp/print");

  return (httpConnect2("localhost", papplSystemGetPort(system), NULL, AF_UNSPEC, HTTP_ENCRYPTION_IF_REQUESTED, 1, 30000, NULL));
}


//
// 'device_error_cb()' - Show a device error message.
//

static void
device_error_cb(const char *message,	// I - Error message
                void       *err_data)	// I - Callback data (unused)
{
  (void)err_data;

  printf("testpappl: %s\n", message);
}


//
// 'device_list_cb()' - List a device.
//

static bool				// O - `true` to stop, `false` to continue
device_list_cb(const char *device_info,	// I - Device description
               const char *device_uri,	// I - Device URI
               const char *device_id,	// I - IEEE-1284 device ID
               void       *data)	// I - Callback data (unused)
{
  (void)data;

  printf("%s\n    %s\n    %s\n", device_info, device_uri, device_id);

  return (false);
}


//
// 'make_raster_file()' - Create a temporary PWG raster file.
//
// Note: Adapted from CUPS "testclient.c"...
//

static const char *                     // O - Print filename
make_raster_file(ipp_t      *response,  // I - Printer attributes
                 bool       grayscale,  // I - Force grayscale?
                 char       *tempname,  // I - Temporary filename buffer
                 size_t     tempsize)   // I - Size of temp file buffer
{
  int                   i,              // Looping var
                        count;          // Number of values
  ipp_attribute_t       *attr;          // Printer attribute
  const char            *type = NULL;   // Raster type (colorspace + bits)
  pwg_media_t           *media = NULL;  // Media size
  int                   xdpi = 0,       // Horizontal resolution
                        ydpi = 0;       // Vertical resolution
  int                   fd;             // Temporary file
  cups_raster_t         *ras;           // Raster stream
  cups_page_header2_t   header;         // Page header
  unsigned char         *line,          // Line of raster data
                        *lineptr;       // Pointer into line
  unsigned              y,              // Current position on page
                        xcount, ycount, // Current count for X and Y
                        xrep, yrep,     // Repeat count for X and Y
                        xoff, yoff,     // Offsets for X and Y
                        yend;           // End Y value
  int                   temprow,        // Row in template
                        tempcolor;      // Template color
  const char            *template;      // Pointer into template
  const unsigned char   *color;         // Current color
  static const unsigned char colors[][3] =
  {                                     // Colors for test
    { 191, 191, 191 },
    { 127, 127, 127 },
    {  63,  63,  63 },
    {   0,   0,   0 },
    { 255,   0,   0 },
    { 255, 127,   0 },
    { 255, 255,   0 },
    { 127, 255,   0 },
    {   0, 255,   0 },
    {   0, 255, 127 },
    {   0, 255, 255 },
    {   0, 127, 255 },
    {   0,   0, 255 },
    { 127,   0, 255 },
    { 255,   0, 255 }
  };
  static const char * const templates[] =
  {                                     // Raster template
    "PPPP     A    PPPP   PPPP   L      TTTTT  EEEEE   SSS   TTTTT          000     1     222    333      4   55555   66    77777   888    999   ",
    "P   P   A A   P   P  P   P  L        T    E      S   S    T           0   0   11    2   2  3   3  4  4   5      6          7  8   8  9   9  ",
    "P   P  A   A  P   P  P   P  L        T    E      S        T           0   0    1        2      3  4  4   5      6         7   8   8  9   9  ",
    "PPPP   AAAAA  PPPP   PPPP   L        T    EEEE    SSS     T           0 0 0    1      22    333   44444   555   6666      7    888    9999  ",
    "P      A   A  P      P      L        T    E          S    T           0   0    1     2         3     4       5  6   6    7    8   8      9  ",
    "P      A   A  P      P      L        T    E      S   S    T           0   0    1    2      3   3     4   5   5  6   6    7    8   8      9  ",
    "P      A   A  P      P      LLLLL    T    EEEEE   SSS     T            000    111   22222   333      4    555    666     7     888     99   ",
    "                                                                                                                                            "
  };


  // Figure out the the media, resolution, and color mode...
  if ((attr = ippFindAttribute(response, "media-ready", IPP_TAG_KEYWORD)) != NULL)
  {
    // Use ready media...
    if (ippContainsString(attr, "na_letter_8.5x11in"))
      media = pwgMediaForPWG("na_letter_8.5x11in");
    else if (ippContainsString(attr, "iso_a4_210x297mm"))
      media = pwgMediaForPWG("iso_a4_210x297mm");
    else
      media = pwgMediaForPWG(ippGetString(attr, 0, NULL));
  }
  else if ((attr = ippFindAttribute(response, "media-default", IPP_TAG_KEYWORD)) != NULL)
  {
    // Use default media...
    media = pwgMediaForPWG(ippGetString(attr, 0, NULL));
  }
  else
  {
    puts("FAIL (No default or ready media reported by printer)");
    return (NULL);
  }

  if ((attr = ippFindAttribute(response, "pwg-raster-document-resolution-supported", IPP_TAG_RESOLUTION)) != NULL)
  {
    for (i = 0, count = ippGetCount(attr); i < count; i ++)
    {
      int tempxdpi, tempydpi;
      ipp_res_t tempunits;

      tempxdpi = ippGetResolution(attr, 0, &tempydpi, &tempunits);

      if (i == 0 || tempxdpi < xdpi || tempydpi < ydpi)
      {
        xdpi = tempxdpi;
        ydpi = tempydpi;
      }
    }

    if ((attr = ippFindAttribute(response, "pwg-raster-document-type-supported", IPP_TAG_KEYWORD)) != NULL)
    {
      if (!grayscale && ippContainsString(attr, "srgb_8"))
        type = "srgb_8";
      else if (ippContainsString(attr, "sgray_8"))
        type = "sgray_8";
    }
  }

  if (xdpi < 72 || ydpi < 72)
  {
    puts("FAIL (No supported raster resolutions)");
    return (NULL);
  }

  if (!type)
  {
    puts("FAIL (No supported color spaces or bit depths)");
    return (NULL);
  }

  // Make the raster context and details...
  if (!cupsRasterInitPWGHeader(&header, media, type, xdpi, ydpi, "one-sided", NULL))
  {
    printf("FAIL (Unable to initialize raster context: %s)\n", cupsRasterErrorString());
    return (NULL);
  }

  header.cupsInteger[CUPS_RASTER_PWG_TotalPageCount] = 1;

  if (header.cupsWidth > (2 * header.HWResolution[0]))
  {
    xoff = header.HWResolution[0] / 2;
    yoff = header.HWResolution[1] / 2;
  }
  else
  {
    xoff = header.HWResolution[0] / 4;
    yoff = header.HWResolution[1] / 4;
  }

  xrep = (header.cupsWidth - 2 * xoff) / 140;
  yrep = xrep * header.HWResolution[1] / header.HWResolution[0];
  yend = header.cupsHeight - yoff;

  // Prepare the raster file...
  if ((line = malloc(header.cupsBytesPerLine)) == NULL)
  {
    printf("FAIL (Unable to allocate %u bytes for raster output: %s)\n", header.cupsBytesPerLine, strerror(errno));
    return (NULL);
  }

  if ((fd = cupsTempFd(tempname, (int)tempsize)) < 0)
  {
    printf("FAIL (Unable to create temporary print file: %s)\n", strerror(errno));
    free(line);
    return (NULL);
  }

  if ((ras = cupsRasterOpen(fd, CUPS_RASTER_WRITE_PWG)) == NULL)
  {
    printf("FAIL (Unable to open raster stream: %s)\n", cupsRasterErrorString());
    close(fd);
    free(line);
    return (NULL);
  }

  // Write a single page consisting of the template dots repeated over the page.
  cupsRasterWriteHeader2(ras, &header);

  memset(line, 0xff, header.cupsBytesPerLine);

  for (y = 0; y < yoff; y ++)
    cupsRasterWritePixels(ras, line, header.cupsBytesPerLine);

  for (temprow = 0, tempcolor = 0; y < yend;)
  {
    template = templates[temprow];
    color    = colors[tempcolor];

    temprow ++;
    if (temprow >= (int)(sizeof(templates) / sizeof(templates[0])))
    {
      temprow = 0;
      tempcolor ++;
      if (tempcolor >= (int)(sizeof(colors) / sizeof(colors[0])))
        tempcolor = 0;
      else if (tempcolor > 3 && header.cupsColorSpace == CUPS_CSPACE_SW)
        tempcolor = 0;
    }

    memset(line, 0xff, header.cupsBytesPerLine);

    if (header.cupsColorSpace == CUPS_CSPACE_SW)
    {
      // Do grayscale output...
      for (lineptr = line + xoff; *template; template ++)
      {
        if (*template != ' ')
        {
          for (xcount = xrep; xcount > 0; xcount --)
            *lineptr++ = *color;
        }
        else
        {
          lineptr += xrep;
        }
      }
    }
    else
    {
      // Do color output...
      for (lineptr = line + 3 * xoff; *template; template ++)
      {
        if (*template != ' ')
        {
          for (xcount = xrep; xcount > 0; xcount --, lineptr += 3)
            memcpy(lineptr, color, 3);
        }
        else
        {
          lineptr += 3 * xrep;
        }
      }
    }

    for (ycount = yrep; ycount > 0 && y < yend; ycount --, y ++)
      cupsRasterWritePixels(ras, line, header.cupsBytesPerLine);
  }

  memset(line, 0xff, header.cupsBytesPerLine);

  for (y = 0; y < header.cupsHeight; y ++)
    cupsRasterWritePixels(ras, line, header.cupsBytesPerLine);

  free(line);

  cupsRasterClose(ras);

  close(fd);

  return (tempname);
}


//
// 'run_tests()' - Run named tests.
//

static void *				// O - Thread status
run_tests(_pappl_testdata_t *testdata)	// I - Testing data
{
  const char	*name;			// Test name
  void		*ret = NULL;		// Return thread status
  cups_dir_t	*dir;			// Output directory
  cups_dentry_t	*dent;			// Output file
  int		files = 0;		// Total file count
  off_t		total = 0;		// Total output size
#ifdef HAVE_LIBJPEG
  static const char * const jpeg_files[] =
  {					// List of JPEG files to print
    "portrait-gray.jpg",
    "portrait-color.jpg",
    "landscape-gray.jpg",
    "landscape-color.jpg"
  };
#endif // HAVE_LIBJPEG
#ifdef HAVE_LIBPNG
  static const char * const png_files[] =
  {					// List of PNG files to print
    "portrait-gray.png",
    "portrait-color.png",
    "landscape-gray.png",
    "landscape-color.png"
  };
#endif // HAVE_LIBPNG

  if (testdata->waitsystem)
  {
    // Wait for the system to start...
    while (!papplSystemIsRunning(testdata->system))
      sleep(1);
  }

  // Run each test...
  for (name = (const char *)cupsArrayFirst(testdata->names); name && !ret && (!papplSystemIsShutdown(testdata->system) || !testdata->waitsystem); name = (const char *)cupsArrayNext(testdata->names))
  {
    printf("%s: ", name);
    fflush(stdout);

    if (!strcmp(name, "api"))
    {
      if (!test_api(testdata->system))
        ret = (void *)1;
      else
        puts("PASS");
    }
    else if (!strcmp(name, "client"))
    {
      if (!test_client(testdata->system))
        ret = (void *)1;
      else
        puts("PASS");
    }
    else if (!strcmp(name, "jpeg"))
    {
#ifdef HAVE_LIBJPEG
      if (!test_image_files(testdata->system, "jpeg", "image/jpeg", (int)(sizeof(jpeg_files) / sizeof(jpeg_files[0])), jpeg_files))
        ret = (void *)1;
      else
        puts("PASS");
#else
      puts("SKIP");
#endif // HAVE_LIBJPEG
    }
    else if (!strcmp(name, "png"))
    {
#ifdef HAVE_LIBPNG
      if (!test_image_files(testdata->system, "png", "image/png", (int)(sizeof(png_files) / sizeof(png_files[0])), png_files))
        ret = (void *)1;
      else
        puts("PASS");
#else
      puts("SKIP");
#endif // HAVE_LIBPNG
    }
    else if (!strcmp(name, "pwg-raster"))
    {
      if (!test_pwg_raster(testdata->system))
        ret = (void *)1;
      else
        puts("PASS");
    }
    else
    {
      puts("UNKNOWN TEST");
      ret = (void *)1;
    }
  }

  // Summarize results...
  if ((dir = cupsDirOpen(testdata->outdirname)) != NULL)
  {
    while ((dent = cupsDirRead(dir)) != NULL)
    {
      if (S_ISREG(dent->fileinfo.st_mode))
      {
        files ++;
        total += dent->fileinfo.st_size;
      }
    }

    cupsDirClose(dir);
  }

  papplSystemShutdown(testdata->system);

  if (ret)
    printf("\nFAILED: %d output file(s), %.1fMB\n", files, total / 1048576.0);
  else
    printf("\nPASSED: %d output file(s), %.1fMB\n", files, total / 1048576.0);

  return (ret);
}


//
// 'test_api()' - Run API unit tests.
//

static bool				// O - `true` on success, `false` on failure
test_api(pappl_system_t *system)	// I - System
{
  bool			pass = true;	// Pass/fail state
  int			i, j;		// Looping vars
  pappl_contact_t	get_contact,	// Contact for "get" call
			set_contact;	// Contact for "set" call
  int			get_int,	// Integer for "get" call
			set_int;	// Integer for "set" call
  char			get_str[1024],	// Temporary string for "get" call
			set_str[1024];	// Temporary string for "set" call
  int			get_nvers;	// Number of versions for "get" call
  pappl_version_t	get_vers[10],	// Versions for "get" call
			set_vers[10];	// Versions for "set" call
  const char		*get_value;	// Value for "get" call
  pappl_loglevel_t	get_loglevel,	// Log level for "get" call
			set_loglevel;	// Log level for "set" call
  size_t		get_size,	// Size for "get" call
			set_size;	// Size for "set" call
  pappl_printer_t	*printer;	// Current printer
  static const char * const set_locations[10][2] =
  {
    // Some wonders of the ancient world (all north-eastern portion of globe...)
    { "Great Pyramid of Giza",        "geo:29.979175,31.134358" },
    { "Temple of Artemis at Ephesus", "geo:37.949722,27.363889" },
    { "Statue of Zeus at Olympia",    "geo:37.637861,21.63" },
    { "Colossus of Rhodes",           "geo:36.451111,28.227778" },
    { "Lighthouse of Alexandria",     "geo:31.213889,29.885556" },

    // Other places
    { "Niagara Falls",                "geo:43.0828201,-79.0763516" },
    { "Grand Canyon",                 "geo:36.0545936,-112.2307085" },
    { "Christ the Redeemer",          "geo:-22.9691208,-43.2583044" },
    { "Great Barrier Reef",           "geo:-16.7546653,143.8322946" },
    { "Science North",                "geo:46.4707,-80.9961" }
  };
  static const char * const set_loglevels[] =
  {					// Log level constants
    "UNSPEC",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
  };


  // papplSystemGet/SetAdminGroup
  fputs("papplSystemGetAdminGroup: ", stdout);
  if (papplSystemGetAdminGroup(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "admin-%d", i);
    printf("api: papplSystemGet/SetAdminGroup('%s'): ", set_str);
    papplSystemSetAdminGroup(system, set_str);
    if (!papplSystemGetAdminGroup(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetAdminGroup(NULL): ", stdout);
  papplSystemSetAdminGroup(system, NULL);
  if (papplSystemGetAdminGroup(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetContact
  fputs("api: papplSystemGetContact: ", stdout);
  if (!papplSystemGetContact(system, &get_contact))
  {
    puts("FAIL (got NULL, expected 'Michael R Sweet')");
    pass = false;
  }
  else if (strcmp(get_contact.name, "Michael R Sweet"))
  {
    printf("FAIL (got '%s', expected 'Michael R Sweet')\n", get_contact.name);
    pass = false;
  }
  else if (strcmp(get_contact.email, "msweet@example.org"))
  {
    printf("FAIL (got '%s', expected 'msweet@example.org')\n", get_contact.email);
    pass = false;
  }
  else if (strcmp(get_contact.telephone, "+1-705-555-1212"))
  {
    printf("FAIL (got '%s', expected '+1-705-555-1212')\n", get_contact.telephone);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_contact.name, sizeof(set_contact.name), "Admin %d", i);
    snprintf(set_contact.email, sizeof(set_contact.email), "admin-%d@example.org", i);
    snprintf(set_contact.telephone, sizeof(set_contact.telephone), "+1-705-555-%04d", i * 1111);

    printf("api: papplSystemGet/SetContact('%s'): ", set_contact.name);
    papplSystemSetContact(system, &set_contact);
    if (!papplSystemGetContact(system, &get_contact))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_contact.name);
      pass = false;
    }
    else if (strcmp(get_contact.name, set_contact.name))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_contact.name, set_contact.name);
      pass = false;
    }
    else if (strcmp(get_contact.email, set_contact.email))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_contact.email, set_contact.email);
      pass = false;
    }
    else if (strcmp(get_contact.telephone, set_contact.telephone))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_contact.telephone, set_contact.telephone);
      pass = false;
    }
    else
      puts("PASS");
  }

  // papplSystemGet/SetDefaultPrinterID
  fputs("api: papplSystemGetDefaultPrinterID: ", stdout);
  if ((get_int = papplSystemGetDefaultPrinterID(system)) == 0)
  {
    puts("FAIL (got 0, expected > 0)");
    pass = false;
  }
  else
    printf("PASS (%d)\n", get_int);

  for (set_int = 2; set_int >= 1; set_int --)
  {
    printf("api: papplSystemSetDefaultPrinterID(%d): ", set_int);
    papplSystemSetDefaultPrinterID(system, set_int);
    if ((get_int = papplSystemGetDefaultPrinterID(system)) != set_int)
    {
      printf("FAIL (got %d, expected %d)\n", get_int, set_int);
      pass = false;
    }
    else
      puts("PASS");
  }

  // papplSystemGet/SetDefaultPrintGroup
  fputs("api: papplSystemGetDefaultPrintGroup: ", stdout);
  if (papplSystemGetDefaultPrintGroup(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "users-%d", i);
    printf("api: papplSystemGet/SetDefaultPrintGroup('%s'): ", set_str);
    papplSystemSetDefaultPrintGroup(system, set_str);
    if (!papplSystemGetDefaultPrintGroup(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetDefaultPrintGroup(NULL): ", stdout);
  papplSystemSetDefaultPrintGroup(system, NULL);
  if (papplSystemGetDefaultPrintGroup(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetDNSSDName
  fputs("api: papplSystemGetDNSSDName: ", stdout);
  if (!papplSystemGetDNSSDName(system, get_str, sizeof(get_str)))
  {
    fputs("FAIL (got NULL, expected 'Test System')\n", stdout);
    pass = false;
  }
  else if (strcmp(get_str, "Test System"))
  {
    printf("FAIL (got '%s', expected 'Test System')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "System Test %c", i + 'A');
    printf("api: papplSystemGet/SetDNSSDName('%s'): ", set_str);
    papplSystemSetDNSSDName(system, set_str);
    if (!papplSystemGetDNSSDName(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetDNSSDName(NULL): ", stdout);
  papplSystemSetDNSSDName(system, NULL);
  if (papplSystemGetDNSSDName(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetFooterHTML
  fputs("api: papplSystemGetFooterHTML: ", stdout);
  if ((get_value = papplSystemGetFooterHTML(system)) == NULL)
  {
    puts("FAIL (got NULL, expected 'Copyright ...')");
    pass = false;
  }
  else if (strncmp(get_value, "Copyright &copy; 2020", 21))
  {
    printf("FAIL (got '%s', expected 'Copyright ...')\n", get_value);
    pass = false;
  }
  else
    puts("PASS");

  fputs("api: papplSystemSetFooterHTML('Mike wuz here.'): ", stdout);
  papplSystemSetFooterHTML(system, "Mike wuz here.");
  if ((get_value = papplSystemGetFooterHTML(system)) == NULL)
  {
    puts("FAIL (got NULL, expected 'Mike wuz here.')");
    pass = false;
  }
  else if (papplSystemIsRunning(system))
  {
    // System is running so we can't change the footer text anymore...
    if (strncmp(get_value, "Copyright &copy; 2020", 21))
    {
      printf("FAIL (got '%s', expected 'Copyright ...')\n", get_value);
      pass = false;
    }
    else
      puts("PASS");
  }
  else
  {
    // System is not running so we can change the footer text...
    if (strcmp(get_value, "Mike wuz here."))
    {
      printf("FAIL (got '%s', expected 'Mike wuz here.')\n", get_value);
      pass = false;
    }
    else
      puts("PASS");
  }

  // papplSystemGet/SetGeoLocation
  fputs("api: papplSystemGetGeoLocation: ", stdout);
  if (!papplSystemGetGeoLocation(system, get_str, sizeof(get_str)))
  {
    puts("FAIL (got NULL, expected 'geo:46.4707,-80.9961')");
    pass = false;
  }
  else if (strcmp(get_str, "geo:46.4707,-80.9961"))
  {
    printf("FAIL (got '%s', expected 'geo:46.4707,-80.9961')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  fputs("api: papplSystemGet/SetGeoLocation('bad-value'): ", stdout);
  papplSystemSetGeoLocation(system, "bad-value");
  if (!papplSystemGetGeoLocation(system, get_str, sizeof(get_str)))
  {
    puts("FAIL (got NULL, expected 'geo:46.4707,-80.9961')");
    pass = false;
  }
  else if (strcmp(get_str, "geo:46.4707,-80.9961"))
  {
    printf("FAIL (got '%s', expected 'geo:46.4707,-80.9961')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < (int)(sizeof(set_locations) / sizeof(set_locations[0])); i ++)
  {
    printf("api: papplSystemGet/SetGeoLocation('%s'): ", set_locations[i][1]);
    papplSystemSetGeoLocation(system, set_locations[i][1]);
    if (!papplSystemGetGeoLocation(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_locations[i][1]);
      pass = false;
    }
    else if (strcmp(get_str, set_locations[i][1]))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_locations[i][1]);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetGeoLocation(NULL): ", stdout);
  papplSystemSetGeoLocation(system, NULL);
  if (papplSystemGetGeoLocation(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetHostname
  fputs("api: papplSystemGetHostname: ", stdout);
  if (!papplSystemGetHostname(system, get_str, sizeof(get_str)))
  {
    fputs("FAIL (got NULL, expected '*.local')\n", stdout);
    pass = false;
  }
  else if (!strstr(get_str, ".local"))
  {
    printf("FAIL (got '%s', expected '*.local')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "example%d.org", i);
    printf("api: papplSystemGet/SetHostname('%s'): ", set_str);
    papplSystemSetHostname(system, set_str);
    if (!papplSystemGetHostname(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetHostname(NULL): ", stdout);
  papplSystemSetHostname(system, NULL);
  if (!papplSystemGetHostname(system, get_str, sizeof(get_str)))
  {
    puts("FAIL (got NULL, expected '*.local')");
    pass = false;
  }
  else if (!strstr(get_str, ".local"))
  {
    printf("FAIL (got '%s', expected '*.local')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetLocation
  fputs("api: papplSystemGetLocation: ", stdout);
  if (!papplSystemGetLocation(system, get_str, sizeof(get_str)))
  {
    fputs("FAIL (got NULL, expected 'Test Lab 42')\n", stdout);
    pass = false;
  }
  else if (strcmp(get_str, "Test Lab 42"))
  {
    printf("FAIL (got '%s', expected 'Test Lab 42')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < (int)(sizeof(set_locations) / sizeof(set_locations[0])); i ++)
  {
    printf("api: papplSystemGet/SetLocation('%s'): ", set_locations[i][0]);
    papplSystemSetLocation(system, set_locations[i][0]);
    if (!papplSystemGetLocation(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_locations[i][0]);
      pass = false;
    }
    else if (strcmp(get_str, set_locations[i][0]))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_locations[i][0]);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetLocation(NULL): ", stdout);
  papplSystemSetLocation(system, NULL);
  if (papplSystemGetLocation(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetLogLevel
  fputs("api: papplSystemGetLogLevel: ", stdout);
  if (papplSystemGetLogLevel(system) == PAPPL_LOGLEVEL_UNSPEC)
  {
    puts("FAIL (got PAPPL_LOGLEVEL_UNSPEC, expected another PAPPL_LOGLEVEL_ value)");
    pass = false;
  }
  else
    puts("PASS");

  for (set_loglevel = PAPPL_LOGLEVEL_FATAL; set_loglevel >= PAPPL_LOGLEVEL_DEBUG; set_loglevel --)
  {
    printf("api: papplSystemSetLogLevel(PAPPL_LOGLEVEL_%s): ", set_loglevels[set_loglevel + 1]);
    papplSystemSetLogLevel(system, set_loglevel);
    if ((get_loglevel = papplSystemGetLogLevel(system)) != set_loglevel)
    {
      printf("FAIL (got PAPPL_LOGLEVEL_%s, expected PAPPL_LOGLEVEL_%s)\n", set_loglevels[get_loglevel + 1], set_loglevels[set_loglevel + 1]);
      pass = false;
    }
    else
      puts("PASS");
  }

  // papplSystemGet/SetMaxLogSize
  fputs("api: papplSystemGetMaxLogSize: ", stdout);
  if ((get_size = papplSystemGetMaxLogSize(system)) != (size_t)(1024 * 1024))
  {
    printf("FAIL (got %ld, expected %ld)\n", (long)get_size, (long)(1024 * 1024));
    pass = false;
  }
  else
    puts("PASS");

  for (set_size = 0; set_size <= (16 * 1024 * 1024); set_size += 1024 * 1024)
  {
    printf("api: papplSystemSetMaxLogSize(%ld): ", (long)set_size);
    papplSystemSetMaxLogSize(system, set_size);
    if ((get_size = papplSystemGetMaxLogSize(system)) != set_size)
    {
      printf("FAIL (got %ld, expected %ld)\n", (long)get_size,  (long)set_size);
      pass = false;
    }
    else
      puts("PASS");
  }

  // papplSystemGet/SetNextPrinterID
  fputs("api: papplSystemGetNextPrinterID: ", stdout);
  if ((get_int = papplSystemGetNextPrinterID(system)) != 3)
  {
    printf("FAIL (got %d, expected 3)\n", get_int);
    pass = false;
  }
  else
    puts("PASS");

  set_int = (TESTRAND % 1000000) + 4;
  printf("api: papplSystemSetNextPrinterID(%d): ", set_int);
  papplSystemSetNextPrinterID(system, set_int);
  if ((get_int = papplSystemGetNextPrinterID(system)) != set_int)
  {
    if (papplSystemIsRunning(system))
      puts("PASS");
    else
    {
      printf("FAIL (got %d, expected %d)\n", get_int, set_int);
      pass = false;
    }
  }
  else
    puts("PASS");

  // papplSystemGet/SetOrganization
  fputs("api: papplSystemGetOrganization: ", stdout);
  if (!papplSystemGetOrganization(system, get_str, sizeof(get_str)))
  {
    puts("FAIL (got NULL, expected 'Lakeside Robotics')");
    pass = false;
  }
  else if (strcmp(get_str, "Lakeside Robotics"))
  {
    printf("FAIL (got '%s', expected 'Lakeside Robotics')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "Organization %c", i + 'A');
    printf("api: papplSystemGet/SetOrganization('%s'): ", set_str);
    papplSystemSetOrganization(system, set_str);
    if (!papplSystemGetOrganization(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetOrganization(NULL): ", stdout);
  papplSystemSetOrganization(system, NULL);
  if (papplSystemGetOrganization(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetOrganizationalUnit
  fputs("api: papplSystemGetOrganizationalUnit: ", stdout);
  if (papplSystemGetOrganizationalUnit(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "%c Team", i + 'A');
    printf("api: papplSystemGet/SetOrganizationalUnit('%s'): ", set_str);
    papplSystemSetOrganizationalUnit(system, set_str);
    if (!papplSystemGetOrganizationalUnit(system, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetOrganizationalUnit(NULL): ", stdout);
  papplSystemSetOrganizationalUnit(system, NULL);
  if (papplSystemGetOrganizationalUnit(system, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplSystemGet/SetUUID
  fputs("api: papplSystemGetUUID: ", stdout);
  if ((get_value = papplSystemGetUUID(system)) == NULL)
  {
    puts("FAIL (got NULL, expected 'urn:uuid:...')");
    pass = false;
  }
  else if (strncmp(get_value, "urn:uuid:", 9))
  {
    printf("FAIL (got '%s', expected 'urn:uuid:...')\n", get_value);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "urn:uuid:%04x%04x-%04x-%04x-%04x-%04x%04x%04x", (unsigned)(TESTRAND % 65536), (unsigned)(TESTRAND % 65536), (unsigned)(TESTRAND % 65536), (unsigned)(TESTRAND % 65536), (unsigned)(TESTRAND % 65536), (unsigned)(TESTRAND % 65536), (unsigned)(TESTRAND % 65536), (unsigned)(TESTRAND % 65536));
    printf("api: papplSystemGet/SetUUID('%s'): ", set_str);
    papplSystemSetUUID(system, set_str);
    if ((get_value = papplSystemGetUUID(system)) == NULL)
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (papplSystemIsRunning(system))
    {
      if (!strcmp(get_value, set_str) || strncmp(get_value, "urn:uuid:", 9))
      {
	printf("FAIL (got '%s', expected different 'urn:uuid:...')\n", get_value);
	pass = false;
      }
      else
        puts("PASS");
    }
    else if (strcmp(get_value, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_value, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplSystemGet/SetUUID(NULL): ", stdout);
  if ((get_value = papplSystemGetUUID(system)) == NULL)
  {
    puts("FAIL (unable to get current UUID)");
    pass = false;
  }
  else
  {
    strlcpy(get_str, get_value, sizeof(get_str));

    papplSystemSetUUID(system, NULL);
    if ((get_value = papplSystemGetUUID(system)) == NULL)
    {
      puts("FAIL (got NULL, expected 'urn:uuid:...')");
      pass = false;
    }
    else if (papplSystemIsRunning(system))
    {
      if (!strcmp(get_value, set_str) || strncmp(get_value, "urn:uuid:", 9))
      {
	printf("FAIL (got '%s', expected different 'urn:uuid:...')\n", get_value);
	pass = false;
      }
      else
	puts("PASS");
    }
    else if (!strcmp(get_value, set_str))
    {
      printf("FAIL (got '%s', expected different '%s')\n", get_value, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  // papplSystemGet/SetVersions
  fputs("api: papplSystemGetVersions: ", stdout);

  if ((get_nvers = papplSystemGetVersions(system, (int)(sizeof(get_vers) / sizeof(get_vers[0])), get_vers)) != 1)
  {
    printf("FAIL (got %d versions, expected 1)\n", get_nvers);
    pass = false;
  }
  else if (strcmp(get_vers[0].name, "Test System") || strcmp(get_vers[0].sversion, "1.0 build 42"))
  {
    printf("FAIL (got '%s v%s', expected 'Test System v1.0 build 42')\n", get_vers[0].name, get_vers[0].sversion);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    printf("api: papplSystemGet/SetVersions(%d): ", i + 1);

    memset(set_vers + i, 0, sizeof(pappl_version_t));
    snprintf(set_vers[i].name, sizeof(set_vers[i].name), "Component %c", 'A' + i);
    set_vers[i].version[0] = (unsigned short)(i + 1);
    set_vers[i].version[1] = (unsigned short)(TESTRAND % 100);
    snprintf(set_vers[i].sversion, sizeof(set_vers[i].sversion), "%u.%02u", set_vers[i].version[0], set_vers[i].version[1]);

    papplSystemSetVersions(system, i + 1, set_vers);

    if ((get_nvers = papplSystemGetVersions(system, (int)(sizeof(get_vers) / sizeof(get_vers[0])), get_vers)) != (i + 1))
    {
      printf("FAIL (got %d versions, expected %d)\n", get_nvers, i + 1);
      pass = false;
    }
    else
    {
      for (j = 0; j < get_nvers; j ++)
      {
        if (strcmp(get_vers[j].name, set_vers[j].name) || strcmp(get_vers[j].sversion, set_vers[j].sversion))
	{
	  printf("FAIL (got '%s v%s', expected '%s v%s')\n", get_vers[j].name, get_vers[j].sversion, set_vers[j].name, set_vers[j].sversion);
	  pass = false;
	  break;
	}
      }

      if (j >= get_nvers)
        puts("PASS");
    }
  }

  // papplSystemFindPrinter
  fputs("api: papplSystemFindPrinter(default): ", stdout);
  if ((printer = papplSystemFindPrinter(system, "/ipp/print", 0, NULL)) == NULL)
  {
    puts("FAIL (got NULL)");
    pass = false;
  }
  else if (papplPrinterGetID(printer) != papplSystemGetDefaultPrinterID(system))
  {
    printf("FAIL (got printer #%d, expected #%d)\n", papplPrinterGetID(printer), papplSystemGetDefaultPrinterID(system));
    pass = false;
  }
  else
    puts("PASS");

  for (set_int = 1; set_int < 3; set_int ++)
  {
    printf("api: papplSystemFindPrinter(%d): ", set_int);
    if ((printer = papplSystemFindPrinter(system, NULL, set_int, NULL)) == NULL)
    {
      puts("FAIL (got NULL)");
      pass = false;
    }
    else
    {
      puts("PASS");
      if (!test_api_printer(printer))
	pass = false;
    }
  }

  // papplPrinterCreate/Delete
  for (i = 0; i < 10; i ++)
  {
    char	name[128];		// Printer name

    snprintf(name, sizeof(name), "test%d", i);
    printf("api: papplPrinterCreate(%s): ", name);
    if ((printer = papplPrinterCreate(system, 0, name, "pwg_common-300dpi-black_1-sgray_8", "MFG:PWG;MDL:Office Printer;CMD:PWGRaster;", "file:///dev/null")) == NULL)
    {
      puts("FAIL (got NULL)");
      pass = false;
    }
    else
    {
      puts("PASS");

      get_int = papplPrinterGetID(printer);

      printf("api: papplPrinterDelete(%s): ", name);
      papplPrinterDelete(printer);

      if (papplSystemFindPrinter(system, NULL, get_int, NULL) != NULL)
      {
        puts("FAIL (printer not deleted)");
        pass = false;
      }
      else
      {
        puts("PASS");

	printf("api: papplPrinterCreate(%s again): ", name);
	if ((printer = papplPrinterCreate(system, 0, name, "pwg_common-300dpi-black_1-sgray_8", "MFG:PWG;MDL:Office Printer;CMD:PWGRaster;", "file:///dev/null")) == NULL)
	{
	  puts("FAIL (got NULL)");
	  pass = false;
	}
	else if (papplPrinterGetID(printer) == get_int)
	{
	  puts("FAIL (got the same printer ID)");
	  pass = false;
	}
	else
	  puts("PASS");
      }
    }
  }

  if (pass)
    fputs("api: ", stdout);

  return (pass);
}


//
// 'test_api_printer()' - Test papplPrinter APIs.
//

static bool				// O - `true` on success, `false` on failure
test_api_printer(
    pappl_printer_t *printer)		// I - Printer
{
  bool			pass = true;	// Pass/fail for tests
  int			i;		// Looping vars
  pappl_contact_t	get_contact,	// Contact for "get" call
			set_contact;	// Contact for "set" call
  int			get_int,	// Integer for "get" call
			set_int;	// Integer for "set" call
  char			get_str[1024],	// Temporary string for "get" call
			set_str[1024];	// Temporary string for "set" call
  static const char * const set_locations[10][2] =
  {
    // Some wonders of the ancient world (all north-eastern portion of globe...)
    { "Great Pyramid of Giza",        "geo:29.979175,31.134358" },
    { "Temple of Artemis at Ephesus", "geo:37.949722,27.363889" },
    { "Statue of Zeus at Olympia",    "geo:37.637861,21.63" },
    { "Colossus of Rhodes",           "geo:36.451111,28.227778" },
    { "Lighthouse of Alexandria",     "geo:31.213889,29.885556" },

    // Other places
    { "Niagara Falls",                "geo:43.0828201,-79.0763516" },
    { "Grand Canyon",                 "geo:36.0545936,-112.2307085" },
    { "Christ the Redeemer",          "geo:-22.9691208,-43.2583044" },
    { "Great Barrier Reef",           "geo:-16.7546653,143.8322946" },
    { "Science North",                "geo:46.4707,-80.9961" }
  };


  // papplPrinterGet/SetContact
  fputs("api: papplPrinterGetContact: ", stdout);
  if (!papplPrinterGetContact(printer, &get_contact))
  {
    puts("FAIL (got NULL, expected 'Michael R Sweet')");
    pass = false;
  }
  else if (strcmp(get_contact.name, "Michael R Sweet"))
  {
    printf("FAIL (got '%s', expected 'Michael R Sweet')\n", get_contact.name);
    pass = false;
  }
  else if (strcmp(get_contact.email, "msweet@example.org"))
  {
    printf("FAIL (got '%s', expected 'msweet@example.org')\n", get_contact.email);
    pass = false;
  }
  else if (strcmp(get_contact.telephone, "+1-705-555-1212"))
  {
    printf("FAIL (got '%s', expected '+1-705-555-1212')\n", get_contact.telephone);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_contact.name, sizeof(set_contact.name), "Admin %d", i);
    snprintf(set_contact.email, sizeof(set_contact.email), "admin-%d@example.org", i);
    snprintf(set_contact.telephone, sizeof(set_contact.telephone), "+1-705-555-%04d", i * 1111);

    printf("api: papplPrinterGet/SetContact('%s'): ", set_contact.name);
    papplPrinterSetContact(printer, &set_contact);
    if (!papplPrinterGetContact(printer, &get_contact))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_contact.name);
      pass = false;
    }
    else if (strcmp(get_contact.name, set_contact.name))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_contact.name, set_contact.name);
      pass = false;
    }
    else if (strcmp(get_contact.email, set_contact.email))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_contact.email, set_contact.email);
      pass = false;
    }
    else if (strcmp(get_contact.telephone, set_contact.telephone))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_contact.telephone, set_contact.telephone);
      pass = false;
    }
    else
      puts("PASS");
  }

  // papplPrinterGet/SetPrintGroup
  fputs("api: papplPrinterGetPrintGroup: ", stdout);
  if (papplPrinterGetPrintGroup(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "users-%d", i);
    printf("api: papplPrinterGet/SetPrintGroup('%s'): ", set_str);
    papplPrinterSetPrintGroup(printer, set_str);
    if (!papplPrinterGetPrintGroup(printer, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplPrinterGet/SetPrintGroup(NULL): ", stdout);
  papplPrinterSetPrintGroup(printer, NULL);
  if (papplPrinterGetPrintGroup(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplPrinterGet/SetDNSSDName
  fputs("api: papplPrinterGetDNSSDName: ", stdout);
  if (!papplPrinterGetDNSSDName(printer, get_str, sizeof(get_str)))
  {
    fputs("FAIL (got NULL, expected string)\n", stdout);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "Printer Test %c", i + 'A');
    printf("api: papplPrinterGet/SetDNSSDName('%s'): ", set_str);
    papplPrinterSetDNSSDName(printer, set_str);
    if (!papplPrinterGetDNSSDName(printer, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplPrinterGet/SetDNSSDName(NULL): ", stdout);
  papplPrinterSetDNSSDName(printer, NULL);
  if (papplPrinterGetDNSSDName(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplPrinterGet/SetGeoLocation
  fputs("api: papplPrinterGetGeoLocation: ", stdout);
  if (!papplPrinterGetGeoLocation(printer, get_str, sizeof(get_str)))
  {
    puts("FAIL (got NULL, expected 'geo:46.4707,-80.9961')");
    pass = false;
  }
  else if (strcmp(get_str, "geo:46.4707,-80.9961"))
  {
    printf("FAIL (got '%s', expected 'geo:46.4707,-80.9961')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  fputs("api: papplPrinterGet/SetGeoLocation('bad-value'): ", stdout);
  papplPrinterSetGeoLocation(printer, "bad-value");
  if (!papplPrinterGetGeoLocation(printer, get_str, sizeof(get_str)))
  {
    puts("FAIL (got NULL, expected 'geo:46.4707,-80.9961')");
    pass = false;
  }
  else if (strcmp(get_str, "geo:46.4707,-80.9961"))
  {
    printf("FAIL (got '%s', expected 'geo:46.4707,-80.9961')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < (int)(sizeof(set_locations) / sizeof(set_locations[0])); i ++)
  {
    printf("api: papplPrinterGet/SetGeoLocation('%s'): ", set_locations[i][1]);
    papplPrinterSetGeoLocation(printer, set_locations[i][1]);
    if (!papplPrinterGetGeoLocation(printer, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_locations[i][1]);
      pass = false;
    }
    else if (strcmp(get_str, set_locations[i][1]))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_locations[i][1]);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplPrinterGet/SetGeoLocation(NULL): ", stdout);
  papplPrinterSetGeoLocation(printer, NULL);
  if (papplPrinterGetGeoLocation(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplPrinterGet/SetLocation
  fputs("api: papplPrinterGetLocation: ", stdout);
  if (!papplPrinterGetLocation(printer, get_str, sizeof(get_str)))
  {
    fputs("FAIL (got NULL, expected 'Test Lab 42')\n", stdout);
    pass = false;
  }
  else if (strcmp(get_str, "Test Lab 42"))
  {
    printf("FAIL (got '%s', expected 'Test Lab 42')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < (int)(sizeof(set_locations) / sizeof(set_locations[0])); i ++)
  {
    printf("api: papplPrinterGet/SetLocation('%s'): ", set_locations[i][0]);
    papplPrinterSetLocation(printer, set_locations[i][0]);
    if (!papplPrinterGetLocation(printer, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_locations[i][0]);
      pass = false;
    }
    else if (strcmp(get_str, set_locations[i][0]))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_locations[i][0]);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplPrinterGet/SetLocation(NULL): ", stdout);
  papplPrinterSetLocation(printer, NULL);
  if (papplPrinterGetLocation(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplPrinterGet/SetNextJobID
  fputs("api: papplPrinterGetNextJobID: ", stdout);
  if ((get_int = papplPrinterGetNextJobID(printer)) != 1)
  {
    printf("FAIL (got %d, expected 1)\n", get_int);
    pass = false;
  }
  else
    puts("PASS");

  set_int = (TESTRAND % 1000000) + 2;
  printf("api: papplPrinterSetNextJobID(%d): ", set_int);
  papplPrinterSetNextJobID(printer, set_int);
  if ((get_int = papplPrinterGetNextJobID(printer)) != set_int)
  {
    printf("FAIL (got %d, expected %d)\n", get_int, set_int);
    pass = false;
  }
  else
    puts("PASS");

  // papplPrinterGet/SetOrganization
  fputs("api: papplPrinterGetOrganization: ", stdout);
  if (!papplPrinterGetOrganization(printer, get_str, sizeof(get_str)))
  {
    puts("FAIL (got NULL, expected 'Lakeside Robotics')");
    pass = false;
  }
  else if (strcmp(get_str, "Lakeside Robotics"))
  {
    printf("FAIL (got '%s', expected 'Lakeside Robotics')\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "Organization %c", i + 'A');
    printf("api: papplPrinterGet/SetOrganization('%s'): ", set_str);
    papplPrinterSetOrganization(printer, set_str);
    if (!papplPrinterGetOrganization(printer, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplPrinterGet/SetOrganization(NULL): ", stdout);
  papplPrinterSetOrganization(printer, NULL);
  if (papplPrinterGetOrganization(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  // papplPrinterGet/SetOrganizationalUnit
  fputs("api: papplPrinterGetOrganizationalUnit: ", stdout);
  if (papplPrinterGetOrganizationalUnit(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  for (i = 0; i < 10; i ++)
  {
    snprintf(set_str, sizeof(set_str), "%c Team", i + 'A');
    printf("api: papplPrinterGet/SetOrganizationalUnit('%s'): ", set_str);
    papplPrinterSetOrganizationalUnit(printer, set_str);
    if (!papplPrinterGetOrganizationalUnit(printer, get_str, sizeof(get_str)))
    {
      printf("FAIL (got NULL, expected '%s')\n", set_str);
      pass = false;
    }
    else if (strcmp(get_str, set_str))
    {
      printf("FAIL (got '%s', expected '%s')\n", get_str, set_str);
      pass = false;
    }
    else
      puts("PASS");
  }

  fputs("api: papplPrinterGet/SetOrganizationalUnit(NULL): ", stdout);
  papplPrinterSetOrganizationalUnit(printer, NULL);
  if (papplPrinterGetOrganizationalUnit(printer, get_str, sizeof(get_str)))
  {
    printf("FAIL (got '%s', expected NULL)\n", get_str);
    pass = false;
  }
  else
    puts("PASS");

  return (pass);
}


//
// 'test_client()' - Run simulated client tests.
//

static bool				// O - `true` on success, `false` on failure
test_client(pappl_system_t *system)	// I - System
{
  http_t	*http;			// HTTP connection
  char		uri[1024];		// "printer-uri" value
  ipp_t		*request,		// Request
		*response;		// Response
  int		i;			// Looping var
  static const char * const pattrs[] =	// Printer attributes
  {
    "printer-contact-col",
    "printer-current-time",
    "printer-geo-location",
    "printer-location",
    "printer-name",
    "printer-state",
    "printer-state-reasons",
    "printer-uuid",
    "printer-uri-supported"
  };
  static const char * const sattrs[] =	// System attributes
  {
    "system-contact-col",
    "system-current-time",
    "system-geo-location",
    "system-location",
    "system-name",
    "system-state",
    "system-state-reasons",
    "system-uuid",
    "system-xri-supported"
  };


  // Connect to system...
  if ((http = connect_to_printer(system, uri, sizeof(uri))) == NULL)
  {
    printf("FAIL (Unable to connect: %s)\n", cupsLastErrorString());
    return (false);
  }

  // Test Get-System-Attributes
  fputs("Get-System-Attributes ", stdout);

  request = ippNewRequest(IPP_OP_GET_SYSTEM_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "system-uri", NULL, "ipp://localhost/ipp/system");
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  response = cupsDoRequest(http, request, "/ipp/system");

  if (cupsLastError() != IPP_STATUS_OK)
  {
    printf("FAIL (%s)\n", cupsLastErrorString());
    httpClose(http);
    ippDelete(response);
    return (false);
  }
  else
  {
    for (i = 0; i < (int)(sizeof(sattrs) / sizeof(sattrs[0])); i ++)
    {
      if (!ippFindAttribute(response, sattrs[i], IPP_TAG_ZERO))
      {
	printf("FAIL (Missing required '%s' attribute in response)\n", sattrs[i]);
	httpClose(http);
	ippDelete(response);
	return (false);
      }
    }

    ippDelete(response);
  }

  // Test Get-Printers
  fputs("\nclient: Get-Printers ", stdout);

  request = ippNewRequest(IPP_OP_GET_PRINTERS);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "system-uri", NULL, "ipp://localhost/ipp/system");
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  response = cupsDoRequest(http, request, "/ipp/system");

  if (cupsLastError() != IPP_STATUS_OK)
  {
    printf("FAIL (%s)\n", cupsLastErrorString());
    httpClose(http);
    ippDelete(response);
    return (false);
  }
  else
  {
    for (i = 0; i < (int)(sizeof(pattrs) / sizeof(pattrs[0])); i ++)
    {
      if (!ippFindAttribute(response, pattrs[i], IPP_TAG_ZERO))
      {
	printf("FAIL (Missing required '%s' attribute in response)\n", sattrs[i]);
	httpClose(http);
	ippDelete(response);
	return (false);
      }
    }

    ippDelete(response);
  }

  // Test Get-Printer-Attributes on /
  fputs("\nclient: Get-Printer-Attributes=/ ", stdout);

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, "ipp://localhost/");
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  response = cupsDoRequest(http, request, "/");

  if (cupsLastError() != IPP_STATUS_OK)
  {
    printf("FAIL (%s)\n", cupsLastErrorString());
    httpClose(http);
    ippDelete(response);
    return (false);
  }
  else
  {
    for (i = 0; i < (int)(sizeof(pattrs) / sizeof(pattrs[0])); i ++)
    {
      if (!ippFindAttribute(response, pattrs[i], IPP_TAG_ZERO))
      {
	printf("FAIL (Missing required '%s' attribute in response)\n", sattrs[i]);
	httpClose(http);
	ippDelete(response);
	return (false);
      }
    }

    ippDelete(response);
  }

  // Test Get-Printer-Attributes on /ipp/print
  fputs("\nclient: Get-Printer-Attributes=/ipp/print ", stdout);

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, "ipp://localhost/ipp/print");
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  response = cupsDoRequest(http, request, "/ipp/print");

  if (cupsLastError() != IPP_STATUS_OK)
  {
    printf("FAIL (%s)\n", cupsLastErrorString());
    httpClose(http);
    ippDelete(response);
    return (false);
  }
  else
  {
    for (i = 0; i < (int)(sizeof(pattrs) / sizeof(pattrs[0])); i ++)
    {
      if (!ippFindAttribute(response, pattrs[i], IPP_TAG_ZERO))
      {
	printf("FAIL (Missing required '%s' attribute in response)\n", sattrs[i]);
	httpClose(http);
	ippDelete(response);
	return (false);
      }
    }

    ippDelete(response);
  }

  httpClose(http);

  return (true);
}


#if defined(HAVE_LIBJPEG) || defined(HAVE_LIBPNG)
//
// 'test_image_files()' - Run image file tests.
//

static bool				// O - `true` on success, `false` on failure
test_image_files(
    pappl_system_t       *system,	// I - System
    const char           *prompt,	// I - Prompt for files
    const char           *format,	// I - MIME media type of files
    int                  num_files,	// I - Number of files to print
    const char * const * files)		// I - Files to print
{
  int		i, j, k, m;		// Looping vars
  http_t	*http;			// HTTP connection
  char		uri[1024],		// "printer-uri" value
		filename[1024],		// Print file
		job_name[1024];		// "job_name" value
  ipp_t		*request,		// Request
		*response;		// Response
  int		job_id;			// "job-id" value
  ipp_jstate_t	job_state;		// "job-state" value
  static const int orients[] =		// "orientation-requested" values
  {
    IPP_ORIENT_NONE,
    IPP_ORIENT_PORTRAIT,
    IPP_ORIENT_LANDSCAPE,
    IPP_ORIENT_REVERSE_PORTRAIT,
    IPP_ORIENT_REVERSE_LANDSCAPE
  };
  static const char * const modes[] =	// "print-color-mode" values
  {
    "auto",
    "color",
    "monochrome"
  };
  static const char * const scalings[] =// "print-scaling" values
  {
    "auto",
    "auto-fit",
    "fill",
    "fit",
    "none"
  };


  // Connect to system...
  if ((http = connect_to_printer(system, uri, sizeof(uri))) == NULL)
  {
    printf("FAIL (Unable to connect: %s)\n", cupsLastErrorString());
    return (false);
  }

  // Print files...
  for (i = 0; i < num_files; i ++)
  {
    if (access(files[i], R_OK))
      snprintf(filename, sizeof(filename), "testsuite/%s", files[i]);
    else
      strlcpy(filename, files[i], sizeof(filename));

    for (j = 0; j < (int)(sizeof(orients) / sizeof(orients[0])); j ++)
    {
      for (k = 0; k < (int)(sizeof(modes) / sizeof(modes[0])); k ++)
      {
	for (m = 0; m < (int)(sizeof(scalings) / sizeof(scalings[0])); m ++)
	{
	  // Stop the test if the system is shutdown (e.g. CTRL+C)
	  if (papplSystemIsShutdown(system))
	    return (false);

	  // Print the job...
	  snprintf(job_name, sizeof(job_name), "%s+%s+%s+%s", files[i], ippEnumString("orientation-requested", orients[j]), modes[k], scalings[m]);

	  request = ippNewRequest(IPP_OP_PRINT_JOB);
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format", NULL, format);
	  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name", NULL, job_name);

          ippAddInteger(request, IPP_TAG_JOB, IPP_TAG_ENUM, "orientation-requested", orients[j]);
	  ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-color-mode", NULL, modes[k]);
	  ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-scaling", NULL, scalings[m]);

	  response = cupsDoFileRequest(http, request, "/ipp/print", filename);

	  if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
	  {
	    printf("FAIL (Unable to print %s: %s)\n", job_name, cupsLastErrorString());
	    ippDelete(response);
	    httpClose(http);
	    return (false);
	  }

	  job_id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);

	  ippDelete(response);

	  printf("%s (job-id=%d)\n%s: ", job_name, job_id, prompt);
	  fflush(stdout);

	  // Poll job status until completed...
	  do
	  {
	    sleep(1);

	    request = ippNewRequest(IPP_OP_GET_JOB_ATTRIBUTES);
	    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
	    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
	    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

	    response = cupsDoRequest(http, request, "/ipp/print");

	    if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
	    {
	      printf("FAIL (Unable to get job state for '%s': %s)\n", job_name, cupsLastErrorString());
	      httpClose(http);
	      ippDelete(response);
	      return (false);
	    }

	    job_state = (ipp_jstate_t)ippGetInteger(ippFindAttribute(response, "job-state", IPP_TAG_ENUM), 0);

	    ippDelete(response);
	  }
	  while (job_state < IPP_JSTATE_CANCELED);
	}
      }
    }
  }

  httpClose(http);

  return (true);
}
#endif // HAVE_LIBJPEG || HAVE_LIBPNG


//
// 'test_pwg_raster()' - Run PWG Raster tests.
//

static bool				// O - `true` on success, `false` on failure
test_pwg_raster(pappl_system_t *system)	// I - System
{
  bool		ret = false;		// Return value
  http_t	*http = NULL;		// HTTP connection
  char		uri[1024],		// "printer-uri" value
		filename[1024] = "",	// Print file
		job_name[1024];		// "job_name" value
  ipp_t		*request,		// IPP request
		*response,		// IPP response
		*supported = NULL;	// Supported attributes
  ipp_attribute_t *mode_supported;	// "print-color-mode-supported" attribute
  int		i;			// Looping var
  int		job_id;			// "job-id" value
  ipp_jstate_t	job_state;		// "job-state" value
  static const char * const modes[] =	// "print-color-mode" values
  {
    "auto",
    "auto-monochrome",
    "color",
    "monochrome"
  };


  // Connect to system...
  if ((http = connect_to_printer(system, uri, sizeof(uri))) == NULL)
  {
    printf("FAIL (Unable to connect: %s)\n", cupsLastErrorString());
    return (false);
  }

  // Get printer capabilities
  fputs("Get-Printer-Attributes: ", stdout);

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, "ipp://localhost/ipp/print");
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  supported = cupsDoRequest(http, request, "/ipp/print");

  if (cupsLastError() != IPP_STATUS_OK)
  {
    printf("FAIL (%s)\n", cupsLastErrorString());
    goto done;
  }

  if ((mode_supported = ippFindAttribute(supported, "print-color-mode-supported", IPP_TAG_KEYWORD)) == NULL)
  {
    puts("FAIL (Missing required 'print-color-mode-supported' attribute in response)");
    goto done;
  }

  // Loop through the supported print-color-mode values...
  for (i = 0; i < (int)(sizeof(modes) / sizeof(modes[0])); i ++)
  {
    // Make raster data for this mode...
    printf("\npwg-raster: %s: ", modes[i]);
    fflush(stdout);

    if (!ippContainsString(mode_supported, modes[i]))
      continue;				// Not supported, skip

    if (!make_raster_file(supported, strstr(modes[i], "monochrome") != NULL, filename, sizeof(filename)))
      break;				// Error

    // Print the file...
    snprintf(job_name, sizeof(job_name), "pwg-raster-%s", modes[i]);

    request = ippNewRequest(IPP_OP_PRINT_JOB);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());
    ippAddString(request, IPP_TAG_OPERATION, IPP_CONST_TAG(IPP_TAG_MIMETYPE), "document-format", NULL, "image/pwg-raster");
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name", NULL, job_name);

    ippAddString(request, IPP_TAG_JOB, IPP_TAG_KEYWORD, "print-color-mode", NULL, modes[i]);

    response = cupsDoFileRequest(http, request, "/ipp/print", filename);

    if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
    {
      printf("FAIL (Unable to print %s: %s)\n", job_name, cupsLastErrorString());
      goto done;
    }

    job_id = ippGetInteger(ippFindAttribute(response, "job-id", IPP_TAG_INTEGER), 0);

    ippDelete(response);

    printf("job-id=%d ", job_id);
    fflush(stdout);

    // Poll job status until completed...
    do
    {
      sleep(1);

      request = ippNewRequest(IPP_OP_GET_JOB_ATTRIBUTES);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
      ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

      response = cupsDoRequest(http, request, "/ipp/print");

      if (cupsLastError() >= IPP_STATUS_ERROR_BAD_REQUEST)
      {
	printf("FAIL (Unable to get job state for '%s': %s)\n", job_name, cupsLastErrorString());
        goto done;
      }

      job_state = (ipp_jstate_t)ippGetInteger(ippFindAttribute(response, "job-state", IPP_TAG_ENUM), 0);

      ippDelete(response);
    }
    while (job_state < IPP_JSTATE_CANCELED);

    // Cleanup...
    unlink(filename);
  }

  // If we complete the loop without errors, it is a successful run...
  ret = true;

  done:

  if (filename[0])
    unlink(filename);

  httpClose(http);
  ippDelete(supported);

  return (ret);
}


//
// 'usage()' - Show usage.
//

static int				// O - Exit status
usage(int status)			// I - Exit status
{
  puts("Usage: testpappl [OPTIONS] [\"SERVER NAME\"]");
  puts("Options:");
  puts("  --help               Show help");
  puts("  --list               List devices");
  puts("  --list-TYPE          Lists devices of TYPE (dns-sd, local, network, usb)");
  puts("  --no-tls             Do not support TLS");
  puts("  --version            Show version");
  puts("  -1                   Single queue");
  puts("  -A PAM-SERVICE       Enable authentication using PAM service");
  puts("  -c                   Do a clean run (no loading of state)");
  puts("  -d SPOOL-DIRECTORY   Set the spool directory");
  puts("  -l LOG-FILE          Set the log file");
  puts("  -L LOG-LEVEL         Set the log level (fatal, error, warn, info, debug)");
  puts("  -m DRIVER-NAME       Add a printer with the named driver");
  puts("  -o OUTPUT-DIRECTORY  Set the output directory (default '.')");
  puts("  -p PORT              Set the listen port (default auto)");
  puts("  -t TEST-NAME         Run the named test (see below)");
  puts("  -T                   Enable TLS-only mode");
  puts("  -U                   Enable USB printer gadget");
  puts("");
  puts("Tests:");
  puts("  all                  All of the following tests");
  puts("  client               Simulated client tests");
  puts("  jpeg                 JPEG image tests");
  puts("  png                  PNG image tests");
  puts("  pwg-raster           PWG Raster tests");

  return (status);
}
