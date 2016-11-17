#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#include <cstdlib>
#include <string>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

/**
 * Given the result of an operation, check to see if 
 * it was successful and, if not, try to generate a 
 * useful error message before throwing and giving up.
 **/
void checkOperation(const int result,
		    const char* const operation,
		    const char* const file,
		    const int line) {

  int errnum = errno;
  
  if (result < 0) {
    std::stringstream msg;
    char buffer[8192];

    msg << "Operation [" << operation << "] failed with return ["
	<< result << "]. Error is [errno := " << errnum << ", msg = ";
    
#ifndef _GNU_SOURCE
    msg << ::strerror_r(errnum, buffer, sizeof(buffer));
#else
    ::strerror_r(errnum, buffer, sizeof(buffer));
    msg << buffer;
#endif

    msg << "]. Failed at [" << file << ":" << line << "].";

    std::cerr << msg.str();
    throw std::runtime_error(msg.str());
  }
}

#define CHECK_OPERATION(op) {			\
  errno = 0;					\
  checkOperation((op), #op, __FILE__, __LINE__);\
  }


void* gpioMmap = nullptr;
volatile uint32_t* gpioBase = nullptr;

void openGPIO() {
  // http://elinux.org/RPi_GPIO_Code_Samples#Direct_register_access
  
  int memoryFD = -1;
  CHECK_OPERATION(memoryFD = ::open("/dev/mem", O_RDWR | O_SYNC));

  // *physical* addresses used to twiddle registers. 
  const uint32_t BCM2708_PERIPHERAL_BASE = 0x3F000000;
  const uint32_t GPIO_BASE = BCM2708_PERIPHERAL_BASE + 0x200000;
  const uint32_t mapSize = 4096;

  //
  // Note: automatically closed when the program exits
  //
  errno = 0;
  gpioMmap = ::mmap(NULL, mapSize, PROT_READ | PROT_WRITE, MAP_SHARED, memoryFD, GPIO_BASE);

  // Remember errno and then close memoryFD since it is no longer needed
  int errnum = errno;
  ::close(memoryFD);

  if (nullptr == gpioMmap) {
    std::stringstream msg;
    char buffer[8192];

    msg << "Memory mapping failed. Error is [errno := " << errnum << ", msg = ";
    
#ifndef _GNU_SOURCE
    msg << ::strerror_r(errnum, buffer, sizeof(buffer));
#else
    ::strerror_r(errnum, buffer, sizeof(buffer));
    msg << buffer;
#endif

    msg << "]";

    std::cerr << msg.str();
    throw std::runtime_error(msg.str());
  }
  
  gpioBase = reinterpret_cast<volatile uint32_t*>(gpioMmap);	   
}

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x)
#define INP_GPIO(g)   *(gpioBase + ((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g)   *(gpioBase + ((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpioBase + (((g)/10))) |= (((a)<=3?(a) + 4:(a)==4?3:2)<<(((g)%10)*3))
 
#define GPIO_SET  *(gpioBase + 7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR  *(gpioBase + 10) // clears bits which are 1 ignores bits which are 0
 
#define GPIO_READ(g)  *(gpioBase + 13) &= (1<<(g))

const int TC_EN = 17;
const int TC_BUSY = 27;


void configureGPIO() {
  // Because of how I have chosen to wire things:
  //
  // Physical pin 1, 17 --> 3.3v power
  //
  // Physical pin 19, 21, 23, 24 --> SPI bus connection
  // to TCM module
  //
  // Physical pin 6, 9 --> grounds
  //
  // Physical pin 11 (BCM17) --> /TC_EN
  //
  // Physical pin 13 (BCM27) --> /TC_BUSY
  //
  // Already configured SPI through other RPI tools so
  // the only things we need to control are /TC_EN and /TC_BUSY
  INP_GPIO(TC_EN);
  INP_GPIO(TC_BUSY);
  OUT_GPIO(TC_EN);
  OUT_GPIO(TC_BUSY);
  
  GPIO_CLR = (1 << TC_EN);
}

int main(int argc, char** argv)
{
  std::cout << "Opening GPIO" << std::endl;
  openGPIO();
  std::cout << "Configuring GPIO" << std::endl;
  configureGPIO();

  std::cout << "/TC_BUSY [" << (GPIO_READ(TC_BUSY))
	    << "]" << std::endl;

  
  // Per data-sheet, sleep 70ms (max init time).
  // At this point, TC_BUSY should read 0
  std::cout << "Waiting for TCM startup" << std::endl;
  usleep(100000);

  std::cout << "Sanity check: /TC_BUSY should be 0 [" << (GPIO_READ(TC_BUSY))
	    << "]" << std::endl;

  
  return 0;
}
