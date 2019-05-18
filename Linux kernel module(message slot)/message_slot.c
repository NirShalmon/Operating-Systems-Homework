// Declare what kind of code we want
// from the header files. Defining __KERNEL__
// and MODULE allows us to access kernel-level
// code not usually available to userspace programs.
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE


#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/slab.h>     /* for kmalloc*/
#include "message_slot.h"

MODULE_LICENSE("GPL");

struct slot{
  int channels_allocated;
  char **messages;
  unsigned char *message_len;
  unsigned int *channel_id; //messages[i] will be the message of channel channel_id[i], channel_id[i] == 0 if channel isn't used yet
};

static struct slot *slots[MAX_MINOR];

//creates a new channel for a slot, at the specified allocated index
//assumes no channel with same id exists
//returns 1 if successful and 0 on faliure
static char create_channel(struct slot *slt, int index,unsigned int id){
  slt->messages[index] = (char*)kmalloc(sizeof(char) * MAX_MESSAGE_LEN,GFP_KERNEL);
  if(!slt->messages[index]){
    printk( KERN_ALERT "memory allocation failed\n");
    return 0;
  }
  slt->message_len[index] = 0;
  slt->channel_id[index] = id;
  return 1;
}

//frees slot memory(incl. it's channels)
static void free_slot(struct slot *slt){
  int i;
  for(i = 0; i < slt->channels_allocated; ++i){
    if(slt->channel_id[i] == 0) break;
    else kfree(slt->messages[i]);
  }
  kfree(slt->channel_id);
  kfree(slt->messages);
  kfree(slt->message_len);
  kfree(slt);
}

//returns NULL if kmalloc fails
static struct slot *create_slot(void){
  struct slot* slt = (struct slot*)kmalloc(sizeof(struct slot),GFP_KERNEL);
  if(!slt) goto slt_fail;
  slt->channels_allocated = 1;
  slt->messages = (char**)kmalloc(sizeof(char*),GFP_KERNEL); //start by allocation 1 channel
  if(!slt->messages) goto msg_fail;
  slt->channel_id = (unsigned int*)kmalloc(sizeof(unsigned int),GFP_KERNEL);
  if(!slt->channel_id) goto id_fail;
  slt->message_len = (unsigned char*)kmalloc(sizeof(unsigned char),GFP_KERNEL);
  if(!slt->message_len) goto len_fail;
  slt->channel_id[0] = 0;
  return slt;

len_fail:
  kfree(slt->channel_id);
id_fail:
  kfree(slt->messages);
msg_fail:
  kfree(slt);
slt_fail:
  printk( KERN_ALERT "memory allocation failed\n");
  return NULL;
}

//allocates more channels for a slot
//returns 1 if succesful or 0 if allocation fails
static char resize_slt(struct slot *slt){
    int i;
    unsigned int *new_ids;
    char **new_messages;
    unsigned char *new_lens;
    new_messages = (char**)krealloc(slt->messages,sizeof(char*)*slt->channels_allocated*2,GFP_KERNEL);
    if(!new_messages){
      printk( KERN_ALERT "memory allocation failed\n");
      return 0;
    }else{
      slt->messages = new_messages;
    }
    new_lens = (unsigned char*)krealloc(slt->message_len,sizeof(unsigned char)*slt->channels_allocated*2,GFP_KERNEL);
    if(!new_lens){
      printk( KERN_ALERT "memory allocation failed\n");
      return 0;
    }else{
      slt->message_len = new_lens;
    }
    new_ids = (unsigned int*)krealloc(slt->channel_id,sizeof(unsigned int)*slt->channels_allocated*2,GFP_KERNEL);
    if(!new_ids){
      printk( KERN_ALERT "memory allocation failed\n");
      return 0;
    }else{
      slt->channel_id = new_ids;
      for(i = slt->channels_allocated; i < slt->channels_allocated*2; ++i){
        slt->channel_id[i] = 0;
      }
    } 
    slt->channels_allocated *= 2;
    return 1;
}



//returns the index of a specified channel of a slot
//creates new channel if new id
//returns -1 if allocation failed
static int get_channel_idx(struct slot *slt, unsigned int channel_id){
  int i;
  for(i = 0; i < slt->channels_allocated; ++i){
    if(slt->channel_id[i] == channel_id){
      return i; //channel found
    }else if(slt->channel_id[i] == 0){
      break;
    }
  }
  //no channel found, and no unused allocated channels
  if(i == slt->channels_allocated){
    if(!resize_slt(slt)) return -1;
  }
  //now we can use the i'th channel
  if(!create_channel(slt,i,channel_id)) return -1;
  return i;
}

//================== DEVICE FUNCTIONS ===========================
static int device_open( struct inode* inode,
                        struct file*  file )
{
  int minor = iminor(inode);
  if(slots[minor] == NULL){ //create new slot if needed
    slots[minor] = create_slot();
    if(!slots[minor]) return -ENOMEM; //bad allocation
  }
  file->private_data = (void*)0; //no channel is set
  return SUCCESS;
}

//---------------------------------------------------------------
static int device_release( struct inode* inode,
                           struct file*  file)
{
  return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read( struct file* file,
                            char __user* buffer,
                            size_t       length,
                            loff_t*      offset )
{
  int minor,channel_idx,i;
  unsigned int channel_id;
  minor = iminor(file_inode(file));
  channel_id = (unsigned int)file->private_data;
  if(channel_id == 0){
    printk( KERN_ALERT "no channel has been set\n");
    return -EINVAL;
  }
  channel_idx = get_channel_idx(slots[minor],channel_id);
  if(channel_idx == -1){
    return -ENOMEM;
  }
  if(slots[minor]->message_len[channel_idx] == 0){
    printk( KERN_ALERT "no message on channel\n");
    return -EWOULDBLOCK;
  }
  if(slots[minor]->message_len[channel_idx] > length){
    printk( KERN_ALERT "buffer too small to contain message\n");
    return -ENOSPC;
  }
  for(i = 0; i < slots[minor]->message_len[channel_idx]; ++i){ //read user buffer into temp buffer(for atomicity)
    if(put_user(slots[minor]->messages[channel_idx][i],buffer+i) == -EFAULT){
      printk( KERN_ALERT "put_user failed\n");
      return -EFAULT;
    }
  }
  return slots[minor]->message_len[channel_idx];
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length,
                             loff_t*            offset)
{
  int minor,channel_idx,i;
  unsigned int channel_id;
  char temp_buffer[MAX_MESSAGE_LEN];
  if(length == 0 || length > MAX_MESSAGE_LEN){
    printk( KERN_ALERT "invalid message length\n");
    return -EMSGSIZE;
  }
  minor = iminor(file_inode(file));
  channel_id = (unsigned int)file->private_data;
  if(channel_id == 0){
    printk( KERN_ALERT "no channel has been set\n");
    return -EINVAL;
  }
  channel_idx = get_channel_idx(slots[minor],channel_id);
  if(channel_idx == -1){
    return -ENOMEM;
  }
  for(i = 0; i < length; ++i){ //read user buffer into temp buffer(for atomicity)
    if(get_user(temp_buffer[i],buffer+i) == -EFAULT){
      printk( KERN_ALERT "get_user failed\n");
      return -EFAULT;
    }
  }
  slots[minor]->message_len[channel_idx] = length;
  for(i = 0; i < length; ++i){ //write buffer to slot
    slots[minor]->messages[channel_idx][i] = temp_buffer[i];
  }
  return length;
}

static long device_ioctl(struct file* file, unsigned int ioctl_command_id,unsigned long ioctl_param){
  if(ioctl_command_id != MSG_SLOT_CHANNEL || ioctl_param == 0){
    return -EINVAL;
  }
  file->private_data = (void*)ioctl_param;
  return 0;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
{
  .read           = device_read,
  .write          = device_write,
  .open           = device_open,
  .release        = device_release,
  .unlocked_ioctl          = device_ioctl
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
  int i;
  int register_ret;
  // Register driver capabilities. Obtain major num
  register_ret = register_chrdev( MAJOR_NUM, DEVICE_RANGE_NAME, &Fops );

  // Negative values signify an error
  if( register_ret < 0 )
  {
    printk( KERN_ALERT "%s registraion failed for  %d\n",
                       DEVICE_FILE_NAME, MAJOR_NUM );
    return register_ret;
  }

  printk( "message_slot: registered major number %d\n", MAJOR_NUM );

  for(i = 0; i < MAX_MINOR; ++i){
    slots[i] = (struct slot*)NULL;
  }

  return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
{
  // free all slots
  int i;
  for(i = 0; i < MAX_MINOR; ++i){
    if(slots[i] != NULL) free_slot(slots[i]);
  }
  // Unregister the device
  // Should always succeed
  unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
