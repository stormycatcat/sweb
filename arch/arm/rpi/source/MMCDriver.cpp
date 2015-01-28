/**
 * @file arch_mmc_driver.cpp
 *
 */
 
#include "BDManager.h"
#include "BDRequest.h"
#include "MMCDriver.h"
#include "ArchInterrupts.h"

#include "Scheduler.h"
#include "kprintf.h"

struct MMCI
{
  uint32 arg2;
  uint32 blksizecnt;
  uint32 arg1;
  uint32 cmdtm;
  uint32 resp0;
  uint32 resp1;
  uint32 resp2;
  uint32 resp3;
  uint32 data;
  uint32 status;
  uint32 control0;
  uint32 control1;
  uint32 interrupt;
  uint32 irpt_mask;
  uint32 irpt_en;
  uint32 control2;
  uint32 force_irpt;
};

struct MMCI* mmci = (struct MMCI*) 0x8C000000;
uint32* mmci_fifo = (uint32*) 0x8C000080;

uint32 mmc_send_cmd(uint32 command, uint32 arg, uint32* response, uint32 data = 0)
{
//  kprintfd("---> command = %d, arg = %x\n",command,arg);
  mmci->arg1 = arg;
  mmci->cmdtm = ((command & 0x3F) << 24) | (response ? (2 << 16) : 0) | (1 << 4) | (data ? (1 << 21) : 0);

  for (uint32 i = 0; i < 0x10000; ++i);
  if (command == 41)
    for (uint32 i = 0; i < 0x100000; ++i);
  while(!(mmci->status & 0x1) && !(mmci->interrupt & 0x1));
//  uint32 temp = 0;
//  kprintfd("interrupt: %x\n",mmci->interrupt);
  mmci->interrupt = 1;
//  kprintfd("status: %x\n",mmci->status);
//  kprintfd("resp0: %x\n",mmci->resp0);
//  kprintfd("resp1: %x\n",mmci->resp1);
//  kprintfd("resp2: %x\n",mmci->resp2);
//  kprintfd("resp3: %x\n",mmci->resp3);
  if (response)
    *response = mmci->resp0;
  return mmci->status;
}

uint32 mmc_send_acmd(uint32 command, uint32 arg, uint32* response)
{
  do
  {
    mmc_send_cmd(55, 0, response);
    *response = 0;
    mmc_send_cmd(command,arg,response);
  }
  while (!((*response) & (1 << 31)));
  return 0;
}

MMCDriver::MMCDriver() : SPT(63), lock_("MMCDriver::lock_"), rca_(0), sector_size_(512), num_sectors_(210672)
{
//  unsigned int check;
  debug(MMC_DRIVER,"MMCDriver()\n");
  uint32 response;
  mmci->control1 = (1 << 24);
  while (mmci->control1 & (1 << 24));
  mmci->control1 = 0xF0F27;
  mmci->control2 = 0x0;
  mmci->blksizecnt = (1 << 16) | sector_size_;
  mmci->irpt_mask = 0xFFFFFFFF;
  while (!(mmci->control1 & (1 << 1)));
  debug(MMC_DRIVER,"MMC controller resetted\n");
  // protocol from sd card specification
  mmc_send_cmd(0,0,0); // go to idle state
  mmc_send_cmd(8,0x1AA,&response,1 << 16); // go to idle state
  mmc_send_acmd(41,0x50FF0000,&response);
  assert((response & 0x80ff8000) == 0x80ff8000);
  mmc_send_cmd(2,0,0);
  mmc_send_cmd(3,0,&response);
  rca_ = response >> 16;
  mmc_send_cmd(7,rca_ << 16,0);
  mmci->irpt_mask = 0xFFFFFFFF;
}

MMCDriver::~MMCDriver()
{

}

uint32 MMCDriver::addRequest( BDRequest * br)
{
  MutexLock lock(lock_);
  debug(MMC_DRIVER, "addRequest %d!\n", br->getCmd() );

  int32 res = -1;

  switch( br->getCmd() )
  {
    case BDRequest::BD_READ:
      res = readSector( br->getStartBlock(), br->getNumBlocks(), br->getBuffer() );
      break;
    case BDRequest::BD_WRITE:
      res = writeSector( br->getStartBlock(), br->getNumBlocks(), br->getBuffer() );
      break;
    default:
      res = -1;
      break;
  }

  debug(MMC_DRIVER, "addRequest:No IRQ operation !!\n");
  br->setStatus( BDRequest::BD_DONE );
  return res;
}

int32 MMCDriver::readBlock ( uint32 address, void *buffer )
{
  debug(MMC_DRIVER,"readBlock: address: %x, buffer: %x\n",address, buffer);
  uint32 response;
  mmc_send_cmd(17,address,&response,1);
  uint32* buffer32 = (uint32*) buffer;
//  uint8* buffer8 = (uint8*) buffer;
  uint32 i = 0;
  while (i < sector_size_ / sizeof(uint32))
  {
    while (!(mmci->interrupt & (1 << 5)));
    buffer32[i++] = mmci->data;
  }
  return 0;
}

int32 MMCDriver::readSector ( uint32 start_sector, uint32 num_sectors, void *buffer )
{
  debug(MMC_DRIVER,"readSector: start: %x, num: %x, buffer: %x\n",start_sector, num_sectors, buffer);
  for (uint32 i = 0; i < num_sectors; ++i)
  {
    readBlock((start_sector + i) * sector_size_, (char*)buffer + i * sector_size_);
  }
  return 0;
}

int32 MMCDriver::writeBlock ( uint32 address __attribute__((unused)), void *buffer __attribute__((unused)))
{
  return 0;
}

int32 MMCDriver::writeSector ( uint32 start_sector __attribute__((unused)), uint32 num_sectors __attribute__((unused)), void * buffer __attribute__((unused)) )
{
  while(1);
  return 0;
}

uint32 MMCDriver::getNumSectors()
{
  return num_sectors_;
}

uint32 MMCDriver::getSectorSize()
{
  return sector_size_;
}

void MMCDriver::serviceIRQ()
{
}
