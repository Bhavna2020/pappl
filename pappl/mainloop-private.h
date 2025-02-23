//
// Private mainloop header file for the Printer Application Framework
//
// Copyright � 2020 by Michael R Sweet.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _PAPPL_MAINLOOP_PRIVATE_H_
#  define _PAPPL_MAINLOOP_PRIVATE_H_

//
// Include necessary headers
//

#  include "mainloop.h"
#  include "base-private.h"


//
// C++ magic...
//

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Globals...
//

extern char *_papplMainloopPath _PAPPL_PRIVATE;


//
// Functions...
//

extern int	_papplMainloopAddPrinter(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopAutoAddPrinters(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopCancelJob(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopDeletePrinter(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopGetSetDefaultPrinter(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopModifyPrinter(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopRunServer(const char *base_name, const char *version, const char *footer_html, int num_drivers, pappl_pr_driver_t *drivers, pappl_pr_autoadd_cb_t autoadd_cb, pappl_pr_driver_cb_t driver_cb, int num_options, cups_option_t *options, pappl_ml_system_cb_t system_cb, void *data) _PAPPL_PRIVATE;
extern int	_papplMainloopShowDevices(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopShowDrivers(const char *base_name, int num_drivers, pappl_pr_driver_t *drivers, pappl_pr_autoadd_cb_t autoadd_cb, pappl_pr_driver_cb_t driver_cb, int num_options, cups_option_t *options, pappl_ml_system_cb_t system_cb, void *data) _PAPPL_PRIVATE;
extern int	_papplMainloopShowJobs(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopShowOptions(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopShowPrinters(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopShowStatus(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopShutdownServer(const char *base_name, int num_options, cups_option_t *options) _PAPPL_PRIVATE;
extern int	_papplMainloopSubmitJob(const char *base_name, int num_options, cups_option_t *options, int num_files, char **files) _PAPPL_PRIVATE;

extern void	_papplMainloopAddOptions(ipp_t *request, int num_options, cups_option_t *options, ipp_t *supported) _PAPPL_PRIVATE;
extern void	_papplMainloopAddPrinterURI(ipp_t *request, const char *printer_name, char *resource,size_t rsize) _PAPPL_PRIVATE;
extern http_t	*_papplMainloopConnect(const char *base_name, bool auto_start) _PAPPL_PRIVATE;
extern http_t	*_papplMainloopConnectURI(const char *base_name, const char *printer_uri, char  *resource, size_t rsize) _PAPPL_PRIVATE;
extern char	*_papplMainloopGetDefaultPrinter(http_t *http, char *buffer, size_t bufsize) _PAPPL_PRIVATE;
extern char	*_papplMainloopGetServerPath(const char *base_name, uid_t uid, char *buffer, size_t bufsize) _PAPPL_PRIVATE;


//
// C++ magic...
//

#  ifdef __cplusplus
}
#  endif // __cplusplus

#  endif // !_PAPPL_MAIN_PRIVATE_H
