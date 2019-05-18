#include<linux/ioctl.h>

#define MAJOR_NUM 51
#define MAX_MINOR 256
#define MAX_MESSAGE_LEN 128
#define MSG_SLOT_CHANNEL _IOR(MAJOR_NUM,0,unsigned int)
#define SUCCESS 0
#define DEVICE_RANGE_NAME "message_slot"
#define BUF_LEN 80
#define DEVICE_FILE_NAME "message_slot"