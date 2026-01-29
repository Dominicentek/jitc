#ifndef __STD_ERRNO_H
#define __STD_ERRNO_H

const int* __errno_location(void);
#define errno (*__errno_location())

#endif
