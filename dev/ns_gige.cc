/*
 * Copyright (c) 2004 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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
 */

/* @file
 * Device module for modelling the National Semiconductor
 * DP83820 ethernet controller.  Does not support priority queueing
 */
#include <cstdio>
#include <deque>
#include <string>

#include "base/inet.hh"
#include "cpu/exec_context.hh"
#include "cpu/intr_control.hh"
#include "dev/dma.hh"
#include "dev/etherlink.hh"
#include "dev/ns_gige.hh"
#include "dev/pciconfigall.hh"
#include "mem/bus/bus.hh"
#include "mem/bus/dma_interface.hh"
#include "mem/bus/pio_interface.hh"
#include "mem/bus/pio_interface_impl.hh"
#include "mem/functional_mem/memory_control.hh"
#include "mem/functional_mem/physical_memory.hh"
#include "sim/builder.hh"
#include "sim/debug.hh"
#include "sim/host.hh"
#include "sim/stats.hh"
#include "targetarch/vtophys.hh"

const char *NsRxStateStrings[] =
{
    "rxIdle",
    "rxDescRefr",
    "rxDescRead",
    "rxFifoBlock",
    "rxFragWrite",
    "rxDescWrite",
    "rxAdvance"
};

const char *NsTxStateStrings[] =
{
    "txIdle",
    "txDescRefr",
    "txDescRead",
    "txFifoBlock",
    "txFragRead",
    "txDescWrite",
    "txAdvance"
};

const char *NsDmaState[] =
{
    "dmaIdle",
    "dmaReading",
    "dmaWriting",
    "dmaReadWaiting",
    "dmaWriteWaiting"
};

using namespace std;
using namespace Net;

///////////////////////////////////////////////////////////////////////
//
// NSGigE PCI Device
//
NSGigE::NSGigE(Params *p)
    : PciDev(p), ioEnable(false),
      txFifo(p->tx_fifo_size), rxFifo(p->rx_fifo_size),
      txPacket(0), rxPacket(0), txPacketBufPtr(NULL), rxPacketBufPtr(NULL),
      txXferLen(0), rxXferLen(0), txState(txIdle), txEnable(false),
      CTDD(false),
      txFragPtr(0), txDescCnt(0), txDmaState(dmaIdle), rxState(rxIdle),
      rxEnable(false), CRDD(false), rxPktBytes(0),
      rxFragPtr(0), rxDescCnt(0), rxDmaState(dmaIdle), extstsEnable(false),
      rxDmaReadEvent(this), rxDmaWriteEvent(this),
      txDmaReadEvent(this), txDmaWriteEvent(this),
      dmaDescFree(p->dma_desc_free), dmaDataFree(p->dma_data_free),
      txDelay(p->tx_delay), rxDelay(p->rx_delay),
      rxKickTick(0), txKickTick(0),
      txEvent(this), rxFilterEnable(p->rx_filter), acceptBroadcast(false),
      acceptMulticast(false), acceptUnicast(false),
      acceptPerfect(false), acceptArp(false),
      physmem(p->pmem), intrTick(0), cpuPendingIntr(false),
      intrEvent(0), interface(0)
{
    if (p->header_bus) {
        pioInterface = newPioInterface(name(), p->hier,
                                       p->header_bus, this,
                                       &NSGigE::cacheAccess);

        pioLatency = p->pio_latency * p->header_bus->clockRatio;

        if (p->payload_bus)
            dmaInterface = new DMAInterface<Bus>(name() + ".dma",
                                                 p->header_bus,
                                                 p->payload_bus, 1);
        else
            dmaInterface = new DMAInterface<Bus>(name() + ".dma",
                                                 p->header_bus,
                                                 p->header_bus, 1);
    } else if (p->payload_bus) {
        pioInterface = newPioInterface(name(), p->hier,
                                       p->payload_bus, this,
                                       &NSGigE::cacheAccess);

        pioLatency = p->pio_latency * p->payload_bus->clockRatio;

        dmaInterface = new DMAInterface<Bus>(name() + ".dma",
                                             p->payload_bus,
                                             p->payload_bus, 1);
    }


    intrDelay = US2Ticks(p->intr_delay);
    dmaReadDelay = p->dma_read_delay;
    dmaWriteDelay = p->dma_write_delay;
    dmaReadFactor = p->dma_read_factor;
    dmaWriteFactor = p->dma_write_factor;

    regsReset();
    memcpy(&rom.perfectMatch, p->eaddr.bytes(), ETH_ADDR_LEN);
}

NSGigE::~NSGigE()
{}

void
NSGigE::regStats()
{
    txBytes
        .name(name() + ".txBytes")
        .desc("Bytes Transmitted")
        .prereq(txBytes)
        ;

    rxBytes
        .name(name() + ".rxBytes")
        .desc("Bytes Received")
        .prereq(rxBytes)
        ;

    txPackets
        .name(name() + ".txPackets")
        .desc("Number of Packets Transmitted")
        .prereq(txBytes)
        ;

    rxPackets
        .name(name() + ".rxPackets")
        .desc("Number of Packets Received")
        .prereq(rxBytes)
        ;

    txIpChecksums
        .name(name() + ".txIpChecksums")
        .desc("Number of tx IP Checksums done by device")
        .precision(0)
        .prereq(txBytes)
        ;

    rxIpChecksums
        .name(name() + ".rxIpChecksums")
        .desc("Number of rx IP Checksums done by device")
        .precision(0)
        .prereq(rxBytes)
        ;

    txTcpChecksums
        .name(name() + ".txTcpChecksums")
        .desc("Number of tx TCP Checksums done by device")
        .precision(0)
        .prereq(txBytes)
        ;

    rxTcpChecksums
        .name(name() + ".rxTcpChecksums")
        .desc("Number of rx TCP Checksums done by device")
        .precision(0)
        .prereq(rxBytes)
        ;

    txUdpChecksums
        .name(name() + ".txUdpChecksums")
        .desc("Number of tx UDP Checksums done by device")
        .precision(0)
        .prereq(txBytes)
        ;

    rxUdpChecksums
        .name(name() + ".rxUdpChecksums")
        .desc("Number of rx UDP Checksums done by device")
        .precision(0)
        .prereq(rxBytes)
        ;

    descDmaReads
        .name(name() + ".descDMAReads")
        .desc("Number of descriptors the device read w/ DMA")
        .precision(0)
        ;

    descDmaWrites
        .name(name() + ".descDMAWrites")
        .desc("Number of descriptors the device wrote w/ DMA")
        .precision(0)
        ;

    descDmaRdBytes
        .name(name() + ".descDmaReadBytes")
        .desc("number of descriptor bytes read w/ DMA")
        .precision(0)
        ;

   descDmaWrBytes
        .name(name() + ".descDmaWriteBytes")
        .desc("number of descriptor bytes write w/ DMA")
        .precision(0)
        ;


    txBandwidth
        .name(name() + ".txBandwidth")
        .desc("Transmit Bandwidth (bits/s)")
        .precision(0)
        .prereq(txBytes)
        ;

    rxBandwidth
        .name(name() + ".rxBandwidth")
        .desc("Receive Bandwidth (bits/s)")
        .precision(0)
        .prereq(rxBytes)
        ;

    txPacketRate
        .name(name() + ".txPPS")
        .desc("Packet Tranmission Rate (packets/s)")
        .precision(0)
        .prereq(txBytes)
        ;

    rxPacketRate
        .name(name() + ".rxPPS")
        .desc("Packet Reception Rate (packets/s)")
        .precision(0)
        .prereq(rxBytes)
        ;

    txBandwidth = txBytes * Stats::constant(8) / simSeconds;
    rxBandwidth = rxBytes * Stats::constant(8) / simSeconds;
    txPacketRate = txPackets / simSeconds;
    rxPacketRate = rxPackets / simSeconds;
}

/**
 * This is to read the PCI general configuration registers
 */
void
NSGigE::ReadConfig(int offset, int size, uint8_t *data)
{
    if (offset < PCI_DEVICE_SPECIFIC)
        PciDev::ReadConfig(offset, size, data);
    else
        panic("Device specific PCI config space not implemented!\n");
}

/**
 * This is to write to the PCI general configuration registers
 */
void
NSGigE::WriteConfig(int offset, int size, uint32_t data)
{
    if (offset < PCI_DEVICE_SPECIFIC)
        PciDev::WriteConfig(offset, size, data);
    else
        panic("Device specific PCI config space not implemented!\n");

    // Need to catch writes to BARs to update the PIO interface
    switch (offset) {
        // seems to work fine without all these PCI settings, but i
        // put in the IO to double check, an assertion will fail if we
        // need to properly implement it
      case PCI_COMMAND:
        if (config.data[offset] & PCI_CMD_IOSE)
            ioEnable = true;
        else
            ioEnable = false;

#if 0
        if (config.data[offset] & PCI_CMD_BME) {
            bmEnabled = true;
        }
        else {
            bmEnabled = false;
        }

        if (config.data[offset] & PCI_CMD_MSE) {
            memEnable = true;
        }
        else {
            memEnable = false;
        }
#endif
        break;

      case PCI0_BASE_ADDR0:
        if (BARAddrs[0] != 0) {
            if (pioInterface)
                pioInterface->addAddrRange(RangeSize(BARAddrs[0], BARSize[0]));

            BARAddrs[0] &= EV5::PAddrUncachedMask;
        }
        break;
      case PCI0_BASE_ADDR1:
        if (BARAddrs[1] != 0) {
            if (pioInterface)
                pioInterface->addAddrRange(RangeSize(BARAddrs[1], BARSize[1]));

            BARAddrs[1] &= EV5::PAddrUncachedMask;
        }
        break;
    }
}

/**
 * This reads the device registers, which are detailed in the NS83820
 * spec sheet
 */
Fault
NSGigE::read(MemReqPtr &req, uint8_t *data)
{
    assert(ioEnable);

    //The mask is to give you only the offset into the device register file
    Addr daddr = req->paddr & 0xfff;
    DPRINTF(EthernetPIO, "read  da=%#x pa=%#x va=%#x size=%d\n",
            daddr, req->paddr, req->vaddr, req->size);


    // there are some reserved registers, you can see ns_gige_reg.h and
    // the spec sheet for details
    if (daddr > LAST && daddr <=  RESERVED) {
        panic("Accessing reserved register");
    } else if (daddr > RESERVED && daddr <= 0x3FC) {
        ReadConfig(daddr & 0xff, req->size, data);
        return No_Fault;
    } else if (daddr >= MIB_START && daddr <= MIB_END) {
        // don't implement all the MIB's.  hopefully the kernel
        // doesn't actually DEPEND upon their values
        // MIB are just hardware stats keepers
        uint32_t &reg = *(uint32_t *) data;
        reg = 0;
        return No_Fault;
    } else if (daddr > 0x3FC)
        panic("Something is messed up!\n");

    switch (req->size) {
      case sizeof(uint32_t):
        {
            uint32_t &reg = *(uint32_t *)data;

            switch (daddr) {
              case CR:
                reg = regs.command;
                //these are supposed to be cleared on a read
                reg &= ~(CR_RXD | CR_TXD | CR_TXR | CR_RXR);
                break;

              case CFG:
                reg = regs.config;
                break;

              case MEAR:
                reg = regs.mear;
                break;

              case PTSCR:
                reg = regs.ptscr;
                break;

              case ISR:
                reg = regs.isr;
                devIntrClear(ISR_ALL);
                break;

              case IMR:
                reg = regs.imr;
                break;

              case IER:
                reg = regs.ier;
                break;

              case IHR:
                reg = regs.ihr;
                break;

              case TXDP:
                reg = regs.txdp;
                break;

              case TXDP_HI:
                reg = regs.txdp_hi;
                break;

              case TXCFG:
                reg = regs.txcfg;
                break;

              case GPIOR:
                reg = regs.gpior;
                break;

              case RXDP:
                reg = regs.rxdp;
                break;

              case RXDP_HI:
                reg = regs.rxdp_hi;
                break;

              case RXCFG:
                reg = regs.rxcfg;
                break;

              case PQCR:
                reg = regs.pqcr;
                break;

              case WCSR:
                reg = regs.wcsr;
                break;

              case PCR:
                reg = regs.pcr;
                break;

                // see the spec sheet for how RFCR and RFDR work
                // basically, you write to RFCR to tell the machine
                // what you want to do next, then you act upon RFDR,
                // and the device will be prepared b/c of what you
                // wrote to RFCR
              case RFCR:
                reg = regs.rfcr;
                break;

              case RFDR:
                switch (regs.rfcr & RFCR_RFADDR) {
                  case 0x000:
                    reg = rom.perfectMatch[1];
                    reg = reg << 8;
                    reg += rom.perfectMatch[0];
                    break;
                  case 0x002:
                    reg = rom.perfectMatch[3] << 8;
                    reg += rom.perfectMatch[2];
                    break;
                  case 0x004:
                    reg = rom.perfectMatch[5] << 8;
                    reg += rom.perfectMatch[4];
                    break;
                  default:
                    panic("reading RFDR for something other than PMATCH!\n");
                    // didn't implement other RFDR functionality b/c
                    // driver didn't use it
                }
                break;

              case SRR:
                reg = regs.srr;
                break;

              case MIBC:
                reg = regs.mibc;
                reg &= ~(MIBC_MIBS | MIBC_ACLR);
                break;

              case VRCR:
                reg = regs.vrcr;
                break;

              case VTCR:
                reg = regs.vtcr;
                break;

              case VDR:
                reg = regs.vdr;
                break;

              case CCSR:
                reg = regs.ccsr;
                break;

              case TBICR:
                reg = regs.tbicr;
                break;

              case TBISR:
                reg = regs.tbisr;
                break;

              case TANAR:
                reg = regs.tanar;
                break;

              case TANLPAR:
                reg = regs.tanlpar;
                break;

              case TANER:
                reg = regs.taner;
                break;

              case TESR:
                reg = regs.tesr;
                break;

              default:
                panic("reading unimplemented register: addr=%#x", daddr);
            }

            DPRINTF(EthernetPIO, "read from %#x: data=%d data=%#x\n",
                    daddr, reg, reg);
        }
        break;

      default:
        panic("accessing register with invalid size: addr=%#x, size=%d",
              daddr, req->size);
    }

    return No_Fault;
}

Fault
NSGigE::write(MemReqPtr &req, const uint8_t *data)
{
    assert(ioEnable);

    Addr daddr = req->paddr & 0xfff;
    DPRINTF(EthernetPIO, "write da=%#x pa=%#x va=%#x size=%d\n",
            daddr, req->paddr, req->vaddr, req->size);

    if (daddr > LAST && daddr <=  RESERVED) {
        panic("Accessing reserved register");
    } else if (daddr > RESERVED && daddr <= 0x3FC) {
        WriteConfig(daddr & 0xff, req->size, *(uint32_t *)data);
        return No_Fault;
    } else if (daddr > 0x3FC)
        panic("Something is messed up!\n");

    if (req->size == sizeof(uint32_t)) {
        uint32_t reg = *(uint32_t *)data;
        DPRINTF(EthernetPIO, "write data=%d data=%#x\n", reg, reg);

        switch (daddr) {
          case CR:
            regs.command = reg;
            if (reg & CR_TXD) {
                txEnable = false;
            } else if (reg & CR_TXE) {
                txEnable = true;

                // the kernel is enabling the transmit machine
                if (txState == txIdle)
                    txKick();
            }

            if (reg & CR_RXD) {
                rxEnable = false;
            } else if (reg & CR_RXE) {
                rxEnable = true;

                if (rxState == rxIdle)
                    rxKick();
            }

            if (reg & CR_TXR)
                txReset();

            if (reg & CR_RXR)
                rxReset();

            if (reg & CR_SWI)
                devIntrPost(ISR_SWI);

            if (reg & CR_RST) {
                txReset();
                rxReset();

                regsReset();
            }
            break;

          case CFG:
            if (reg & CFG_LNKSTS ||
                reg & CFG_SPDSTS ||
                reg & CFG_DUPSTS ||
                reg & CFG_RESERVED ||
                reg & CFG_T64ADDR ||
                reg & CFG_PCI64_DET)
                panic("writing to read-only or reserved CFG bits!\n");

            regs.config |= reg & ~(CFG_LNKSTS | CFG_SPDSTS | CFG_DUPSTS |
                                   CFG_RESERVED | CFG_T64ADDR | CFG_PCI64_DET);

// all these #if 0's are because i don't THINK the kernel needs to
// have these implemented. if there is a problem relating to one of
// these, you may need to add functionality in.
#if 0
            if (reg & CFG_TBI_EN) ;
            if (reg & CFG_MODE_1000) ;
#endif

            if (reg & CFG_AUTO_1000)
                panic("CFG_AUTO_1000 not implemented!\n");

#if 0
            if (reg & CFG_PINT_DUPSTS ||
                reg & CFG_PINT_LNKSTS ||
                reg & CFG_PINT_SPDSTS)
                ;

            if (reg & CFG_TMRTEST) ;
            if (reg & CFG_MRM_DIS) ;
            if (reg & CFG_MWI_DIS) ;

            if (reg & CFG_T64ADDR)
                panic("CFG_T64ADDR is read only register!\n");

            if (reg & CFG_PCI64_DET)
                panic("CFG_PCI64_DET is read only register!\n");

            if (reg & CFG_DATA64_EN) ;
            if (reg & CFG_M64ADDR) ;
            if (reg & CFG_PHY_RST) ;
            if (reg & CFG_PHY_DIS) ;
#endif

            if (reg & CFG_EXTSTS_EN)
                extstsEnable = true;
            else
                extstsEnable = false;

#if 0
              if (reg & CFG_REQALG) ;
              if (reg & CFG_SB) ;
              if (reg & CFG_POW) ;
              if (reg & CFG_EXD) ;
              if (reg & CFG_PESEL) ;
              if (reg & CFG_BROM_DIS) ;
              if (reg & CFG_EXT_125) ;
              if (reg & CFG_BEM) ;
#endif
            break;

          case MEAR:
            regs.mear = reg;
            // since phy is completely faked, MEAR_MD* don't matter
            // and since the driver never uses MEAR_EE*, they don't
            // matter
#if 0
            if (reg & MEAR_EEDI) ;
            if (reg & MEAR_EEDO) ; // this one is read only
            if (reg & MEAR_EECLK) ;
            if (reg & MEAR_EESEL) ;
            if (reg & MEAR_MDIO) ;
            if (reg & MEAR_MDDIR) ;
            if (reg & MEAR_MDC) ;
#endif
            break;

          case PTSCR:
            regs.ptscr = reg & ~(PTSCR_RBIST_RDONLY);
            // these control BISTs for various parts of chip - we
            // don't care or do just fake that the BIST is done
            if (reg & PTSCR_RBIST_EN)
                regs.ptscr |= PTSCR_RBIST_DONE;
            if (reg & PTSCR_EEBIST_EN)
                regs.ptscr &= ~PTSCR_EEBIST_EN;
            if (reg & PTSCR_EELOAD_EN)
                regs.ptscr &= ~PTSCR_EELOAD_EN;
            break;

          case ISR: /* writing to the ISR has no effect */
            panic("ISR is a read only register!\n");

          case IMR:
            regs.imr = reg;
            devIntrChangeMask();
            break;

          case IER:
            regs.ier = reg;
            break;

          case IHR:
            regs.ihr = reg;
            /* not going to implement real interrupt holdoff */
            break;

          case TXDP:
            regs.txdp = (reg & 0xFFFFFFFC);
            assert(txState == txIdle);
            CTDD = false;
            break;

          case TXDP_HI:
            regs.txdp_hi = reg;
            break;

          case TXCFG:
            regs.txcfg = reg;
#if 0
            if (reg & TXCFG_CSI) ;
            if (reg & TXCFG_HBI) ;
            if (reg & TXCFG_MLB) ;
            if (reg & TXCFG_ATP) ;
            if (reg & TXCFG_ECRETRY) {
                /*
                 * this could easily be implemented, but considering
                 * the network is just a fake pipe, wouldn't make
                 * sense to do this
                 */
            }

            if (reg & TXCFG_BRST_DIS) ;
#endif

#if 0
            /* we handle our own DMA, ignore the kernel's exhortations */
            if (reg & TXCFG_MXDMA) ;
#endif

            // also, we currently don't care about fill/drain
            // thresholds though this may change in the future with
            // more realistic networks or a driver which changes it
            // according to feedback

            break;

          case GPIOR:
            regs.gpior = reg;
            /* these just control general purpose i/o pins, don't matter */
            break;

          case RXDP:
            regs.rxdp = reg;
            CRDD = false;
            break;

          case RXDP_HI:
            regs.rxdp_hi = reg;
            break;

          case RXCFG:
            regs.rxcfg = reg;
#if 0
            if (reg & RXCFG_AEP) ;
            if (reg & RXCFG_ARP) ;
            if (reg & RXCFG_STRIPCRC) ;
            if (reg & RXCFG_RX_RD) ;
            if (reg & RXCFG_ALP) ;
            if (reg & RXCFG_AIRL) ;

            /* we handle our own DMA, ignore what kernel says about it */
            if (reg & RXCFG_MXDMA) ;

            //also, we currently don't care about fill/drain thresholds
            //though this may change in the future with more realistic
            //networks or a driver which changes it according to feedback
            if (reg & (RXCFG_DRTH | RXCFG_DRTH0)) ;
#endif
            break;

          case PQCR:
            /* there is no priority queueing used in the linux 2.6 driver */
            regs.pqcr = reg;
            break;

          case WCSR:
            /* not going to implement wake on LAN */
            regs.wcsr = reg;
            break;

          case PCR:
            /* not going to implement pause control */
            regs.pcr = reg;
            break;

          case RFCR:
            regs.rfcr = reg;

            rxFilterEnable = (reg & RFCR_RFEN) ? true : false;
            acceptBroadcast = (reg & RFCR_AAB) ? true : false;
            acceptMulticast = (reg & RFCR_AAM) ? true : false;
            acceptUnicast = (reg & RFCR_AAU) ? true : false;
            acceptPerfect = (reg & RFCR_APM) ? true : false;
            acceptArp = (reg & RFCR_AARP) ? true : false;

#if 0
            if (reg & RFCR_APAT)
                panic("RFCR_APAT not implemented!\n");
#endif

            if (reg & RFCR_MHEN || reg & RFCR_UHEN)
                panic("hash filtering not implemented!\n");

            if (reg & RFCR_ULM)
                panic("RFCR_ULM not implemented!\n");

            break;

          case RFDR:
            panic("the driver never writes to RFDR, something is wrong!\n");

          case BRAR:
            panic("the driver never uses BRAR, something is wrong!\n");

          case BRDR:
            panic("the driver never uses BRDR, something is wrong!\n");

          case SRR:
            panic("SRR is read only register!\n");

          case MIBC:
            panic("the driver never uses MIBC, something is wrong!\n");

          case VRCR:
            regs.vrcr = reg;
            break;

          case VTCR:
            regs.vtcr = reg;
            break;

          case VDR:
            panic("the driver never uses VDR, something is wrong!\n");
            break;

          case CCSR:
            /* not going to implement clockrun stuff */
            regs.ccsr = reg;
            break;

          case TBICR:
            regs.tbicr = reg;
            if (reg & TBICR_MR_LOOPBACK)
                panic("TBICR_MR_LOOPBACK never used, something wrong!\n");

            if (reg & TBICR_MR_AN_ENABLE) {
                regs.tanlpar = regs.tanar;
                regs.tbisr |= (TBISR_MR_AN_COMPLETE | TBISR_MR_LINK_STATUS);
            }

#if 0
            if (reg & TBICR_MR_RESTART_AN) ;
#endif

            break;

          case TBISR:
            panic("TBISR is read only register!\n");

          case TANAR:
            regs.tanar = reg;
            if (reg & TANAR_PS2)
                panic("this isn't used in driver, something wrong!\n");

            if (reg & TANAR_PS1)
                panic("this isn't used in driver, something wrong!\n");
            break;

          case TANLPAR:
            panic("this should only be written to by the fake phy!\n");

          case TANER:
            panic("TANER is read only register!\n");

          case TESR:
            regs.tesr = reg;
            break;

          default:
            panic("invalid register access daddr=%#x", daddr);
        }
    } else {
        panic("Invalid Request Size");
    }

    return No_Fault;
}

void
NSGigE::devIntrPost(uint32_t interrupts)
{
    if (interrupts & ISR_RESERVE)
        panic("Cannot set a reserved interrupt");

    if (interrupts & ISR_NOIMPL)
        warn("interrupt not implemented %#x\n", interrupts);

    interrupts &= ~ISR_NOIMPL;
    regs.isr |= interrupts;

    DPRINTF(EthernetIntr,
            "interrupt written to ISR: intr=%#x isr=%#x imr=%#x\n",
            interrupts, regs.isr, regs.imr);

    if ((regs.isr & regs.imr)) {
        Tick when = curTick;
        if (!(regs.isr & regs.imr & ISR_NODELAY))
            when += intrDelay;
        cpuIntrPost(when);
    }
}

void
NSGigE::devIntrClear(uint32_t interrupts)
{
    if (interrupts & ISR_RESERVE)
        panic("Cannot clear a reserved interrupt");

    interrupts &= ~ISR_NOIMPL;
    regs.isr &= ~interrupts;

    DPRINTF(EthernetIntr,
            "interrupt cleared from ISR: intr=%x isr=%x imr=%x\n",
            interrupts, regs.isr, regs.imr);

    if (!(regs.isr & regs.imr))
        cpuIntrClear();
}

void
NSGigE::devIntrChangeMask()
{
    DPRINTF(EthernetIntr, "interrupt mask changed: isr=%x imr=%x masked=%x\n",
            regs.isr, regs.imr, regs.isr & regs.imr);

    if (regs.isr & regs.imr)
        cpuIntrPost(curTick);
    else
        cpuIntrClear();
}

void
NSGigE::cpuIntrPost(Tick when)
{
    // If the interrupt you want to post is later than an interrupt
    // already scheduled, just let it post in the coming one and don't
    // schedule another.
    // HOWEVER, must be sure that the scheduled intrTick is in the
    // future (this was formerly the source of a bug)
    /**
     * @todo this warning should be removed and the intrTick code should
     * be fixed.
     */
    assert(when >= curTick);
    assert(intrTick >= curTick || intrTick == 0);
    if (when > intrTick && intrTick != 0) {
        DPRINTF(EthernetIntr, "don't need to schedule event...intrTick=%d\n",
                intrTick);
        return;
    }

    intrTick = when;
    if (intrTick < curTick) {
        debug_break();
        intrTick = curTick;
    }

    DPRINTF(EthernetIntr, "going to schedule an interrupt for intrTick=%d\n",
            intrTick);

    if (intrEvent)
        intrEvent->squash();
    intrEvent = new IntrEvent(this, true);
    intrEvent->schedule(intrTick);
}

void
NSGigE::cpuInterrupt()
{
    assert(intrTick == curTick);

    // Whether or not there's a pending interrupt, we don't care about
    // it anymore
    intrEvent = 0;
    intrTick = 0;

    // Don't send an interrupt if there's already one
    if (cpuPendingIntr) {
        DPRINTF(EthernetIntr,
                "would send an interrupt now, but there's already pending\n");
    } else {
        // Send interrupt
        cpuPendingIntr = true;

        DPRINTF(EthernetIntr, "posting interrupt\n");
        intrPost();
    }
}

void
NSGigE::cpuIntrClear()
{
    if (!cpuPendingIntr)
        return;

    if (intrEvent) {
        intrEvent->squash();
        intrEvent = 0;
    }

    intrTick = 0;

    cpuPendingIntr = false;

    DPRINTF(EthernetIntr, "clearing interrupt\n");
    intrClear();
}

bool
NSGigE::cpuIntrPending() const
{ return cpuPendingIntr; }

void
NSGigE::txReset()
{

    DPRINTF(Ethernet, "transmit reset\n");

    CTDD = false;
    txEnable = false;;
    txFragPtr = 0;
    assert(txDescCnt == 0);
    txFifo.clear();
    txState = txIdle;
    assert(txDmaState == dmaIdle);
}

void
NSGigE::rxReset()
{
    DPRINTF(Ethernet, "receive reset\n");

    CRDD = false;
    assert(rxPktBytes == 0);
    rxEnable = false;
    rxFragPtr = 0;
    assert(rxDescCnt == 0);
    assert(rxDmaState == dmaIdle);
    rxFifo.clear();
    rxState = rxIdle;
}

void
NSGigE::regsReset()
{
    memset(&regs, 0, sizeof(regs));
    regs.config = CFG_LNKSTS;
    regs.mear = MEAR_MDDIR | MEAR_EEDO;
    regs.txcfg = 0x120; // set drain threshold to 1024 bytes and
                        // fill threshold to 32 bytes
    regs.rxcfg = 0x4;   // set drain threshold to 16 bytes
    regs.srr = 0x0103;  // set the silicon revision to rev B or 0x103
    regs.mibc = MIBC_FRZ;
    regs.vdr = 0x81;    // set the vlan tag type to 802.1q
    regs.tesr = 0xc000; // TBI capable of both full and half duplex

    extstsEnable = false;
    acceptBroadcast = false;
    acceptMulticast = false;
    acceptUnicast = false;
    acceptPerfect = false;
    acceptArp = false;
}

void
NSGigE::rxDmaReadCopy()
{
    assert(rxDmaState == dmaReading);

    physmem->dma_read((uint8_t *)rxDmaData, rxDmaAddr, rxDmaLen);
    rxDmaState = dmaIdle;

    DPRINTF(EthernetDMA, "rx dma read  paddr=%#x len=%d\n",
            rxDmaAddr, rxDmaLen);
    DDUMP(EthernetDMA, rxDmaData, rxDmaLen);
}

bool
NSGigE::doRxDmaRead()
{
    assert(rxDmaState == dmaIdle || rxDmaState == dmaReadWaiting);
    rxDmaState = dmaReading;

    if (dmaInterface && !rxDmaFree) {
        if (dmaInterface->busy())
            rxDmaState = dmaReadWaiting;
        else
            dmaInterface->doDMA(Read, rxDmaAddr, rxDmaLen, curTick,
                                &rxDmaReadEvent, true);
        return true;
    }

    if (dmaReadDelay == 0 && dmaReadFactor == 0) {
        rxDmaReadCopy();
        return false;
    }

    Tick factor = ((rxDmaLen + ULL(63)) >> ULL(6)) * dmaReadFactor;
    Tick start = curTick + dmaReadDelay + factor;
    rxDmaReadEvent.schedule(start);
    return true;
}

void
NSGigE::rxDmaReadDone()
{
    assert(rxDmaState == dmaReading);
    rxDmaReadCopy();

    // If the transmit state machine has a pending DMA, let it go first
    if (txDmaState == dmaReadWaiting || txDmaState == dmaWriteWaiting)
        txKick();

    rxKick();
}

void
NSGigE::rxDmaWriteCopy()
{
    assert(rxDmaState == dmaWriting);

    physmem->dma_write(rxDmaAddr, (uint8_t *)rxDmaData, rxDmaLen);
    rxDmaState = dmaIdle;

    DPRINTF(EthernetDMA, "rx dma write paddr=%#x len=%d\n",
            rxDmaAddr, rxDmaLen);
    DDUMP(EthernetDMA, rxDmaData, rxDmaLen);
}

bool
NSGigE::doRxDmaWrite()
{
    assert(rxDmaState == dmaIdle || rxDmaState == dmaWriteWaiting);
    rxDmaState = dmaWriting;

    if (dmaInterface && !rxDmaFree) {
        if (dmaInterface->busy())
            rxDmaState = dmaWriteWaiting;
        else
            dmaInterface->doDMA(WriteInvalidate, rxDmaAddr, rxDmaLen, curTick,
                                &rxDmaWriteEvent, true);
        return true;
    }

    if (dmaWriteDelay == 0 && dmaWriteFactor == 0) {
        rxDmaWriteCopy();
        return false;
    }

    Tick factor = ((rxDmaLen + ULL(63)) >> ULL(6)) * dmaWriteFactor;
    Tick start = curTick + dmaWriteDelay + factor;
    rxDmaWriteEvent.schedule(start);
    return true;
}

void
NSGigE::rxDmaWriteDone()
{
    assert(rxDmaState == dmaWriting);
    rxDmaWriteCopy();

    // If the transmit state machine has a pending DMA, let it go first
    if (txDmaState == dmaReadWaiting || txDmaState == dmaWriteWaiting)
        txKick();

    rxKick();
}

void
NSGigE::rxKick()
{
    DPRINTF(EthernetSM, "receive kick rxState=%s (rxBuf.size=%d)\n",
            NsRxStateStrings[rxState], rxFifo.size());

    if (rxKickTick > curTick) {
        DPRINTF(EthernetSM, "receive kick exiting, can't run till %d\n",
                rxKickTick);
        return;
    }

  next:
    switch(rxDmaState) {
      case dmaReadWaiting:
        if (doRxDmaRead())
            goto exit;
        break;
      case dmaWriteWaiting:
        if (doRxDmaWrite())
            goto exit;
        break;
      default:
        break;
    }

    // see state machine from spec for details
    // the way this works is, if you finish work on one state and can
    // go directly to another, you do that through jumping to the
    // label "next".  however, if you have intermediate work, like DMA
    // so that you can't go to the next state yet, you go to exit and
    // exit the loop.  however, when the DMA is done it will trigger
    // an event and come back to this loop.
    switch (rxState) {
      case rxIdle:
        if (!rxEnable) {
            DPRINTF(EthernetSM, "Receive Disabled! Nothing to do.\n");
            goto exit;
        }

        if (CRDD) {
            rxState = rxDescRefr;

            rxDmaAddr = regs.rxdp & 0x3fffffff;
            rxDmaData = &rxDescCache + offsetof(ns_desc, link);
            rxDmaLen = sizeof(rxDescCache.link);
            rxDmaFree = dmaDescFree;

            descDmaReads++;
            descDmaRdBytes += rxDmaLen;

            if (doRxDmaRead())
                goto exit;
        } else {
            rxState = rxDescRead;

            rxDmaAddr = regs.rxdp & 0x3fffffff;
            rxDmaData = &rxDescCache;
            rxDmaLen = sizeof(ns_desc);
            rxDmaFree = dmaDescFree;

            descDmaReads++;
            descDmaRdBytes += rxDmaLen;

            if (doRxDmaRead())
                goto exit;
        }
        break;

      case rxDescRefr:
        if (rxDmaState != dmaIdle)
            goto exit;

        rxState = rxAdvance;
        break;

     case rxDescRead:
        if (rxDmaState != dmaIdle)
            goto exit;

        DPRINTF(EthernetDesc,
                "rxDescCache: addr=%08x read descriptor\n",
                regs.rxdp & 0x3fffffff);
        DPRINTF(EthernetDesc,
                "rxDescCache: link=%08x bufptr=%08x cmdsts=%08x extsts=%08x\n",
                rxDescCache.link, rxDescCache.bufptr, rxDescCache.cmdsts,
                rxDescCache.extsts);

        if (rxDescCache.cmdsts & CMDSTS_OWN) {
            devIntrPost(ISR_RXIDLE);
            rxState = rxIdle;
            goto exit;
        } else {
            rxState = rxFifoBlock;
            rxFragPtr = rxDescCache.bufptr;
            rxDescCnt = rxDescCache.cmdsts & CMDSTS_LEN_MASK;
        }
        break;

      case rxFifoBlock:
        if (!rxPacket) {
            /**
             * @todo in reality, we should be able to start processing
             * the packet as it arrives, and not have to wait for the
             * full packet ot be in the receive fifo.
             */
            if (rxFifo.empty())
                goto exit;

            DPRINTF(EthernetSM, "****processing receive of new packet****\n");

            // If we don't have a packet, grab a new one from the fifo.
            rxPacket = rxFifo.front();
            rxPktBytes = rxPacket->length;
            rxPacketBufPtr = rxPacket->data;

#if TRACING_ON
            if (DTRACE(Ethernet)) {
                IpPtr ip(rxPacket);
                if (ip) {
                    DPRINTF(Ethernet, "ID is %d\n", ip->id());
                    TcpPtr tcp(ip);
                    if (tcp) {
                        DPRINTF(Ethernet, "Src Port=%d, Dest Port=%d\n",
                                tcp->sport(), tcp->dport());
                    }
                }
            }
#endif

            // sanity check - i think the driver behaves like this
            assert(rxDescCnt >= rxPktBytes);
            rxFifo.pop();
        }


        // dont' need the && rxDescCnt > 0 if driver sanity check
        // above holds
        if (rxPktBytes > 0) {
            rxState = rxFragWrite;
            // don't need min<>(rxPktBytes,rxDescCnt) if above sanity
            // check holds
            rxXferLen = rxPktBytes;

            rxDmaAddr = rxFragPtr & 0x3fffffff;
            rxDmaData = rxPacketBufPtr;
            rxDmaLen = rxXferLen;
            rxDmaFree = dmaDataFree;

            if (doRxDmaWrite())
                goto exit;

        } else {
            rxState = rxDescWrite;

            //if (rxPktBytes == 0) {  /* packet is done */
            assert(rxPktBytes == 0);
            DPRINTF(EthernetSM, "done with receiving packet\n");

            rxDescCache.cmdsts |= CMDSTS_OWN;
            rxDescCache.cmdsts &= ~CMDSTS_MORE;
            rxDescCache.cmdsts |= CMDSTS_OK;
            rxDescCache.cmdsts &= 0xffff0000;
            rxDescCache.cmdsts += rxPacket->length;   //i.e. set CMDSTS_SIZE

#if 0
            /*
             * all the driver uses these are for its own stats keeping
             * which we don't care about, aren't necessary for
             * functionality and doing this would just slow us down.
             * if they end up using this in a later version for
             * functional purposes, just undef
             */
            if (rxFilterEnable) {
                rxDescCache.cmdsts &= ~CMDSTS_DEST_MASK;
                const EthAddr &dst = rxFifoFront()->dst();
                if (dst->unicast())
                    rxDescCache.cmdsts |= CMDSTS_DEST_SELF;
                if (dst->multicast())
                    rxDescCache.cmdsts |= CMDSTS_DEST_MULTI;
                if (dst->broadcast())
                    rxDescCache.cmdsts |= CMDSTS_DEST_MASK;
            }
#endif

            IpPtr ip(rxPacket);
            if (extstsEnable && ip) {
                rxDescCache.extsts |= EXTSTS_IPPKT;
                rxIpChecksums++;
                if (cksum(ip) != 0) {
                    DPRINTF(EthernetCksum, "Rx IP Checksum Error\n");
                    rxDescCache.extsts |= EXTSTS_IPERR;
                }
                TcpPtr tcp(ip);
                UdpPtr udp(ip);
                if (tcp) {
                    rxDescCache.extsts |= EXTSTS_TCPPKT;
                    rxTcpChecksums++;
                    if (cksum(tcp) != 0) {
                        DPRINTF(EthernetCksum, "Rx TCP Checksum Error\n");
                        rxDescCache.extsts |= EXTSTS_TCPERR;

                    }
                } else if (udp) {
                    rxDescCache.extsts |= EXTSTS_UDPPKT;
                    rxUdpChecksums++;
                    if (cksum(udp) != 0) {
                        DPRINTF(EthernetCksum, "Rx UDP Checksum Error\n");
                        rxDescCache.extsts |= EXTSTS_UDPERR;
                    }
                }
            }
            rxPacket = 0;

            /*
             * the driver seems to always receive into desc buffers
             * of size 1514, so you never have a pkt that is split
             * into multiple descriptors on the receive side, so
             * i don't implement that case, hence the assert above.
             */

            DPRINTF(EthernetDesc,
                    "rxDescCache: addr=%08x writeback cmdsts extsts\n",
                    regs.rxdp & 0x3fffffff);
            DPRINTF(EthernetDesc,
                    "rxDescCache: link=%08x bufptr=%08x cmdsts=%08x extsts=%08x\n",
                    rxDescCache.link, rxDescCache.bufptr, rxDescCache.cmdsts,
                    rxDescCache.extsts);

            rxDmaAddr = (regs.rxdp + offsetof(ns_desc, cmdsts)) & 0x3fffffff;
            rxDmaData = &(rxDescCache.cmdsts);
            rxDmaLen = sizeof(rxDescCache.cmdsts) + sizeof(rxDescCache.extsts);
            rxDmaFree = dmaDescFree;

            descDmaWrites++;
            descDmaWrBytes += rxDmaLen;

            if (doRxDmaWrite())
                goto exit;
        }
        break;

      case rxFragWrite:
        if (rxDmaState != dmaIdle)
            goto exit;

        rxPacketBufPtr += rxXferLen;
        rxFragPtr += rxXferLen;
        rxPktBytes -= rxXferLen;

        rxState = rxFifoBlock;
        break;

      case rxDescWrite:
        if (rxDmaState != dmaIdle)
            goto exit;

        assert(rxDescCache.cmdsts & CMDSTS_OWN);

        assert(rxPacket == 0);
        devIntrPost(ISR_RXOK);

        if (rxDescCache.cmdsts & CMDSTS_INTR)
            devIntrPost(ISR_RXDESC);

        if (!rxEnable) {
            DPRINTF(EthernetSM, "Halting the RX state machine\n");
            rxState = rxIdle;
            goto exit;
        } else
            rxState = rxAdvance;
        break;

      case rxAdvance:
        if (rxDescCache.link == 0) {
            devIntrPost(ISR_RXIDLE);
            rxState = rxIdle;
            CRDD = true;
            goto exit;
        } else {
            rxState = rxDescRead;
            regs.rxdp = rxDescCache.link;
            CRDD = false;

            rxDmaAddr = regs.rxdp & 0x3fffffff;
            rxDmaData = &rxDescCache;
            rxDmaLen = sizeof(ns_desc);
            rxDmaFree = dmaDescFree;

            if (doRxDmaRead())
                goto exit;
        }
        break;

      default:
        panic("Invalid rxState!");
    }

    DPRINTF(EthernetSM, "entering next rxState=%s\n",
            NsRxStateStrings[rxState]);

    goto next;

  exit:
    /**
     * @todo do we want to schedule a future kick?
     */
    DPRINTF(EthernetSM, "rx state machine exited rxState=%s\n",
            NsRxStateStrings[rxState]);
}

void
NSGigE::transmit()
{
    if (txFifo.empty()) {
        DPRINTF(Ethernet, "nothing to transmit\n");
        return;
    }

    DPRINTF(Ethernet, "Attempt Pkt Transmit: txFifo length=%d\n",
            txFifo.size());
    if (interface->sendPacket(txFifo.front())) {
#if TRACING_ON
        if (DTRACE(Ethernet)) {
            IpPtr ip(txFifo.front());
            if (ip) {
                DPRINTF(Ethernet, "ID is %d\n", ip->id());
                TcpPtr tcp(ip);
                if (tcp) {
                    DPRINTF(Ethernet, "Src Port=%d, Dest Port=%d\n",
                            tcp->sport(), tcp->dport());
                }
            }
        }
#endif

        DDUMP(Ethernet, txFifo.front()->data, txFifo.front()->length);
        txBytes += txFifo.front()->length;
        txPackets++;

        DPRINTF(Ethernet, "Successful Xmit! now txFifoAvail is %d\n",
                txFifo.avail());
        txFifo.pop();

        /*
         * normally do a writeback of the descriptor here, and ONLY
         * after that is done, send this interrupt.  but since our
         * stuff never actually fails, just do this interrupt here,
         * otherwise the code has to stray from this nice format.
         * besides, it's functionally the same.
         */
        devIntrPost(ISR_TXOK);
    }

   if (!txFifo.empty() && !txEvent.scheduled()) {
       DPRINTF(Ethernet, "reschedule transmit\n");
       txEvent.schedule(curTick + 1000);
   }
}

void
NSGigE::txDmaReadCopy()
{
    assert(txDmaState == dmaReading);

    physmem->dma_read((uint8_t *)txDmaData, txDmaAddr, txDmaLen);
    txDmaState = dmaIdle;

    DPRINTF(EthernetDMA, "tx dma read  paddr=%#x len=%d\n",
            txDmaAddr, txDmaLen);
    DDUMP(EthernetDMA, txDmaData, txDmaLen);
}

bool
NSGigE::doTxDmaRead()
{
    assert(txDmaState == dmaIdle || txDmaState == dmaReadWaiting);
    txDmaState = dmaReading;

    if (dmaInterface && !txDmaFree) {
        if (dmaInterface->busy())
            txDmaState = dmaReadWaiting;
        else
            dmaInterface->doDMA(Read, txDmaAddr, txDmaLen, curTick,
                                &txDmaReadEvent, true);
        return true;
    }

    if (dmaReadDelay == 0 && dmaReadFactor == 0.0) {
        txDmaReadCopy();
        return false;
    }

    Tick factor = ((txDmaLen + ULL(63)) >> ULL(6)) * dmaReadFactor;
    Tick start = curTick + dmaReadDelay + factor;
    txDmaReadEvent.schedule(start);
    return true;
}

void
NSGigE::txDmaReadDone()
{
    assert(txDmaState == dmaReading);
    txDmaReadCopy();

    // If the receive state machine  has a pending DMA, let it go first
    if (rxDmaState == dmaReadWaiting || rxDmaState == dmaWriteWaiting)
        rxKick();

    txKick();
}

void
NSGigE::txDmaWriteCopy()
{
    assert(txDmaState == dmaWriting);

    physmem->dma_write(txDmaAddr, (uint8_t *)txDmaData, txDmaLen);
    txDmaState = dmaIdle;

    DPRINTF(EthernetDMA, "tx dma write paddr=%#x len=%d\n",
            txDmaAddr, txDmaLen);
    DDUMP(EthernetDMA, txDmaData, txDmaLen);
}

bool
NSGigE::doTxDmaWrite()
{
    assert(txDmaState == dmaIdle || txDmaState == dmaWriteWaiting);
    txDmaState = dmaWriting;

    if (dmaInterface && !txDmaFree) {
        if (dmaInterface->busy())
            txDmaState = dmaWriteWaiting;
        else
            dmaInterface->doDMA(WriteInvalidate, txDmaAddr, txDmaLen, curTick,
                                &txDmaWriteEvent, true);
        return true;
    }

    if (dmaWriteDelay == 0 && dmaWriteFactor == 0.0) {
        txDmaWriteCopy();
        return false;
    }

    Tick factor = ((txDmaLen + ULL(63)) >> ULL(6)) * dmaWriteFactor;
    Tick start = curTick + dmaWriteDelay + factor;
    txDmaWriteEvent.schedule(start);
    return true;
}

void
NSGigE::txDmaWriteDone()
{
    assert(txDmaState == dmaWriting);
    txDmaWriteCopy();

    // If the receive state machine  has a pending DMA, let it go first
    if (rxDmaState == dmaReadWaiting || rxDmaState == dmaWriteWaiting)
        rxKick();

    txKick();
}

void
NSGigE::txKick()
{
    DPRINTF(EthernetSM, "transmit kick txState=%s\n",
            NsTxStateStrings[txState]);

    if (txKickTick > curTick) {
        DPRINTF(EthernetSM, "transmit kick exiting, can't run till %d\n",
                txKickTick);

        return;
    }

  next:
    switch(txDmaState) {
      case dmaReadWaiting:
        if (doTxDmaRead())
            goto exit;
        break;
      case dmaWriteWaiting:
        if (doTxDmaWrite())
            goto exit;
        break;
      default:
        break;
    }

    switch (txState) {
      case txIdle:
        if (!txEnable) {
            DPRINTF(EthernetSM, "Transmit disabled.  Nothing to do.\n");
            goto exit;
        }

        if (CTDD) {
            txState = txDescRefr;

            txDmaAddr = regs.txdp & 0x3fffffff;
            txDmaData = &txDescCache + offsetof(ns_desc, link);
            txDmaLen = sizeof(txDescCache.link);
            txDmaFree = dmaDescFree;

            descDmaReads++;
            descDmaRdBytes += txDmaLen;

            if (doTxDmaRead())
                goto exit;

        } else {
            txState = txDescRead;

            txDmaAddr = regs.txdp & 0x3fffffff;
            txDmaData = &txDescCache;
            txDmaLen = sizeof(ns_desc);
            txDmaFree = dmaDescFree;

            descDmaReads++;
            descDmaRdBytes += txDmaLen;

            if (doTxDmaRead())
                goto exit;
        }
        break;

      case txDescRefr:
        if (txDmaState != dmaIdle)
            goto exit;

        txState = txAdvance;
        break;

      case txDescRead:
        if (txDmaState != dmaIdle)
            goto exit;

        DPRINTF(EthernetDesc,
                "txDescCache: link=%08x bufptr=%08x cmdsts=%08x extsts=%08x\n",
                txDescCache.link, txDescCache.bufptr, txDescCache.cmdsts,
                txDescCache.extsts);

        if (txDescCache.cmdsts & CMDSTS_OWN) {
            txState = txFifoBlock;
            txFragPtr = txDescCache.bufptr;
            txDescCnt = txDescCache.cmdsts & CMDSTS_LEN_MASK;
        } else {
            devIntrPost(ISR_TXIDLE);
            txState = txIdle;
            goto exit;
        }
        break;

      case txFifoBlock:
        if (!txPacket) {
            DPRINTF(EthernetSM, "****starting the tx of a new packet****\n");
            txPacket = new PacketData(16384);
            txPacketBufPtr = txPacket->data;
        }

        if (txDescCnt == 0) {
            DPRINTF(EthernetSM, "the txDescCnt == 0, done with descriptor\n");
            if (txDescCache.cmdsts & CMDSTS_MORE) {
                DPRINTF(EthernetSM, "there are more descriptors to come\n");
                txState = txDescWrite;

                txDescCache.cmdsts &= ~CMDSTS_OWN;

                txDmaAddr = regs.txdp + offsetof(ns_desc, cmdsts);
                txDmaAddr &= 0x3fffffff;
                txDmaData = &(txDescCache.cmdsts);
                txDmaLen = sizeof(txDescCache.cmdsts);
                txDmaFree = dmaDescFree;

                if (doTxDmaWrite())
                    goto exit;

            } else { /* this packet is totally done */
                DPRINTF(EthernetSM, "This packet is done, let's wrap it up\n");
                /* deal with the the packet that just finished */
                if ((regs.vtcr & VTCR_PPCHK) && extstsEnable) {
                    IpPtr ip(txPacket);
                    if (txDescCache.extsts & EXTSTS_UDPPKT) {
                        UdpPtr udp(ip);
                        udp->sum(0);
                        udp->sum(cksum(udp));
                        txUdpChecksums++;
                    } else if (txDescCache.extsts & EXTSTS_TCPPKT) {
                        TcpPtr tcp(ip);
                        tcp->sum(0);
                        tcp->sum(cksum(tcp));
                        txTcpChecksums++;
                    }
                    if (txDescCache.extsts & EXTSTS_IPPKT) {
                        ip->sum(0);
                        ip->sum(cksum(ip));
                        txIpChecksums++;
                    }
                }

                txPacket->length = txPacketBufPtr - txPacket->data;
                // this is just because the receive can't handle a
                // packet bigger want to make sure
                assert(txPacket->length <= 1514);
#ifndef NDEBUG
                bool success =
#endif
                    txFifo.push(txPacket);
                assert(success);

                /*
                 * this following section is not tqo spec, but
                 * functionally shouldn't be any different.  normally,
                 * the chip will wait til the transmit has occurred
                 * before writing back the descriptor because it has
                 * to wait to see that it was successfully transmitted
                 * to decide whether to set CMDSTS_OK or not.
                 * however, in the simulator since it is always
                 * successfully transmitted, and writing it exactly to
                 * spec would complicate the code, we just do it here
                 */

                txDescCache.cmdsts &= ~CMDSTS_OWN;
                txDescCache.cmdsts |= CMDSTS_OK;

                DPRINTF(EthernetDesc,
                        "txDesc writeback: cmdsts=%08x extsts=%08x\n",
                        txDescCache.cmdsts, txDescCache.extsts);

                txDmaAddr = regs.txdp + offsetof(ns_desc, cmdsts);
                txDmaAddr &= 0x3fffffff;
                txDmaData = &(txDescCache.cmdsts);
                txDmaLen = sizeof(txDescCache.cmdsts) +
                    sizeof(txDescCache.extsts);
                txDmaFree = dmaDescFree;

                descDmaWrites++;
                descDmaWrBytes += txDmaLen;

                transmit();
                txPacket = 0;

                if (!txEnable) {
                    DPRINTF(EthernetSM, "halting TX state machine\n");
                    txState = txIdle;
                    goto exit;
                } else
                    txState = txAdvance;

                if (doTxDmaWrite())
                    goto exit;
            }
        } else {
            DPRINTF(EthernetSM, "this descriptor isn't done yet\n");
            if (!txFifo.full()) {
                txState = txFragRead;

                /*
                 * The number of bytes transferred is either whatever
                 * is left in the descriptor (txDescCnt), or if there
                 * is not enough room in the fifo, just whatever room
                 * is left in the fifo
                 */
                txXferLen = min<uint32_t>(txDescCnt, txFifo.avail());

                txDmaAddr = txFragPtr & 0x3fffffff;
                txDmaData = txPacketBufPtr;
                txDmaLen = txXferLen;
                txDmaFree = dmaDataFree;

                if (doTxDmaRead())
                    goto exit;
            } else {
                txState = txFifoBlock;
                transmit();

                goto exit;
            }

        }
        break;

      case txFragRead:
        if (txDmaState != dmaIdle)
            goto exit;

        txPacketBufPtr += txXferLen;
        txFragPtr += txXferLen;
        txDescCnt -= txXferLen;
        txFifo.reserve(txXferLen);

        txState = txFifoBlock;
        break;

      case txDescWrite:
        if (txDmaState != dmaIdle)
            goto exit;

        if (txDescCache.cmdsts & CMDSTS_INTR)
            devIntrPost(ISR_TXDESC);

        txState = txAdvance;
        break;

      case txAdvance:
        if (txDescCache.link == 0) {
            devIntrPost(ISR_TXIDLE);
            txState = txIdle;
            goto exit;
        } else {
            txState = txDescRead;
            regs.txdp = txDescCache.link;
            CTDD = false;

            txDmaAddr = txDescCache.link & 0x3fffffff;
            txDmaData = &txDescCache;
            txDmaLen = sizeof(ns_desc);
            txDmaFree = dmaDescFree;

            if (doTxDmaRead())
                goto exit;
        }
        break;

      default:
        panic("invalid state");
    }

    DPRINTF(EthernetSM, "entering next txState=%s\n",
            NsTxStateStrings[txState]);

    goto next;

  exit:
    /**
     * @todo do we want to schedule a future kick?
     */
    DPRINTF(EthernetSM, "tx state machine exited txState=%s\n",
            NsTxStateStrings[txState]);
}

void
NSGigE::transferDone()
{
    if (txFifo.empty()) {
        DPRINTF(Ethernet, "transfer complete: txFifo empty...nothing to do\n");
        return;
    }

    DPRINTF(Ethernet, "transfer complete: data in txFifo...schedule xmit\n");

    if (txEvent.scheduled())
        txEvent.reschedule(curTick + 1);
    else
        txEvent.schedule(curTick + 1);
}

bool
NSGigE::rxFilter(const PacketPtr &packet)
{
    EthPtr eth = packet;
    bool drop = true;
    string type;

    const EthAddr &dst = eth->dst();
    if (dst.unicast()) {
        // If we're accepting all unicast addresses
        if (acceptUnicast)
            drop = false;

        // If we make a perfect match
        if (acceptPerfect && dst == rom.perfectMatch)
            drop = false;

        if (acceptArp && eth->type() == ETH_TYPE_ARP)
            drop = false;

    } else if (dst.broadcast()) {
        // if we're accepting broadcasts
        if (acceptBroadcast)
            drop = false;

    } else if (dst.multicast()) {
        // if we're accepting all multicasts
        if (acceptMulticast)
            drop = false;

    }

    if (drop) {
        DPRINTF(Ethernet, "rxFilter drop\n");
        DDUMP(EthernetData, packet->data, packet->length);
    }

    return drop;
}

bool
NSGigE::recvPacket(PacketPtr packet)
{
    rxBytes += packet->length;
    rxPackets++;

    DPRINTF(Ethernet, "Receiving packet from wire, rxFifoAvail=%d\n",
            rxFifo.avail());

    if (!rxEnable) {
        DPRINTF(Ethernet, "receive disabled...packet dropped\n");
        debug_break();
        interface->recvDone();
        return true;
    }

    if (rxFilterEnable && rxFilter(packet)) {
        DPRINTF(Ethernet, "packet filtered...dropped\n");
        interface->recvDone();
        return true;
    }

    if (rxFifo.avail() < packet->length) {
        DPRINTF(Ethernet,
                "packet will not fit in receive buffer...packet dropped\n");
        devIntrPost(ISR_RXORN);
        return false;
    }

    rxFifo.push(packet);
    interface->recvDone();

    rxKick();
    return true;
}

//=====================================================================
//
//
void
NSGigE::serialize(ostream &os)
{
    // Serialize the PciDev base class
    PciDev::serialize(os);

    /*
     * Finalize any DMA events now.
     */
    if (rxDmaReadEvent.scheduled())
        rxDmaReadCopy();
    if (rxDmaWriteEvent.scheduled())
        rxDmaWriteCopy();
    if (txDmaReadEvent.scheduled())
        txDmaReadCopy();
    if (txDmaWriteEvent.scheduled())
        txDmaWriteCopy();

    /*
     * Serialize the device registers
     */
    SERIALIZE_SCALAR(regs.command);
    SERIALIZE_SCALAR(regs.config);
    SERIALIZE_SCALAR(regs.mear);
    SERIALIZE_SCALAR(regs.ptscr);
    SERIALIZE_SCALAR(regs.isr);
    SERIALIZE_SCALAR(regs.imr);
    SERIALIZE_SCALAR(regs.ier);
    SERIALIZE_SCALAR(regs.ihr);
    SERIALIZE_SCALAR(regs.txdp);
    SERIALIZE_SCALAR(regs.txdp_hi);
    SERIALIZE_SCALAR(regs.txcfg);
    SERIALIZE_SCALAR(regs.gpior);
    SERIALIZE_SCALAR(regs.rxdp);
    SERIALIZE_SCALAR(regs.rxdp_hi);
    SERIALIZE_SCALAR(regs.rxcfg);
    SERIALIZE_SCALAR(regs.pqcr);
    SERIALIZE_SCALAR(regs.wcsr);
    SERIALIZE_SCALAR(regs.pcr);
    SERIALIZE_SCALAR(regs.rfcr);
    SERIALIZE_SCALAR(regs.rfdr);
    SERIALIZE_SCALAR(regs.srr);
    SERIALIZE_SCALAR(regs.mibc);
    SERIALIZE_SCALAR(regs.vrcr);
    SERIALIZE_SCALAR(regs.vtcr);
    SERIALIZE_SCALAR(regs.vdr);
    SERIALIZE_SCALAR(regs.ccsr);
    SERIALIZE_SCALAR(regs.tbicr);
    SERIALIZE_SCALAR(regs.tbisr);
    SERIALIZE_SCALAR(regs.tanar);
    SERIALIZE_SCALAR(regs.tanlpar);
    SERIALIZE_SCALAR(regs.taner);
    SERIALIZE_SCALAR(regs.tesr);

    SERIALIZE_ARRAY(rom.perfectMatch, ETH_ADDR_LEN);

    SERIALIZE_SCALAR(ioEnable);

    /*
     * Serialize the data Fifos
     */
    rxFifo.serialize("rxFifo", os);
    txFifo.serialize("txFifo", os);

    /*
     * Serialize the various helper variables
     */
    bool txPacketExists = txPacket;
    SERIALIZE_SCALAR(txPacketExists);
    if (txPacketExists) {
        txPacket->serialize("txPacket", os);
        uint32_t txPktBufPtr = (uint32_t) (txPacketBufPtr - txPacket->data);
        SERIALIZE_SCALAR(txPktBufPtr);
    }

    bool rxPacketExists = rxPacket;
    SERIALIZE_SCALAR(rxPacketExists);
    if (rxPacketExists) {
        rxPacket->serialize("rxPacket", os);
        uint32_t rxPktBufPtr = (uint32_t) (rxPacketBufPtr - rxPacket->data);
        SERIALIZE_SCALAR(rxPktBufPtr);
    }

    SERIALIZE_SCALAR(txXferLen);
    SERIALIZE_SCALAR(rxXferLen);

    /*
     * Serialize DescCaches
     */
    SERIALIZE_SCALAR(txDescCache.link);
    SERIALIZE_SCALAR(txDescCache.bufptr);
    SERIALIZE_SCALAR(txDescCache.cmdsts);
    SERIALIZE_SCALAR(txDescCache.extsts);
    SERIALIZE_SCALAR(rxDescCache.link);
    SERIALIZE_SCALAR(rxDescCache.bufptr);
    SERIALIZE_SCALAR(rxDescCache.cmdsts);
    SERIALIZE_SCALAR(rxDescCache.extsts);

    /*
     * Serialize tx state machine
     */
    int txState = this->txState;
    SERIALIZE_SCALAR(txState);
    SERIALIZE_SCALAR(txEnable);
    SERIALIZE_SCALAR(CTDD);
    SERIALIZE_SCALAR(txFragPtr);
    SERIALIZE_SCALAR(txDescCnt);
    int txDmaState = this->txDmaState;
    SERIALIZE_SCALAR(txDmaState);

    /*
     * Serialize rx state machine
     */
    int rxState = this->rxState;
    SERIALIZE_SCALAR(rxState);
    SERIALIZE_SCALAR(rxEnable);
    SERIALIZE_SCALAR(CRDD);
    SERIALIZE_SCALAR(rxPktBytes);
    SERIALIZE_SCALAR(rxFragPtr);
    SERIALIZE_SCALAR(rxDescCnt);
    int rxDmaState = this->rxDmaState;
    SERIALIZE_SCALAR(rxDmaState);

    SERIALIZE_SCALAR(extstsEnable);

    /*
     * If there's a pending transmit, store the time so we can
     * reschedule it later
     */
    Tick transmitTick = txEvent.scheduled() ? txEvent.when() - curTick : 0;
    SERIALIZE_SCALAR(transmitTick);

    /*
     * receive address filter settings
     */
    SERIALIZE_SCALAR(rxFilterEnable);
    SERIALIZE_SCALAR(acceptBroadcast);
    SERIALIZE_SCALAR(acceptMulticast);
    SERIALIZE_SCALAR(acceptUnicast);
    SERIALIZE_SCALAR(acceptPerfect);
    SERIALIZE_SCALAR(acceptArp);

    /*
     * Keep track of pending interrupt status.
     */
    SERIALIZE_SCALAR(intrTick);
    SERIALIZE_SCALAR(cpuPendingIntr);
    Tick intrEventTick = 0;
    if (intrEvent)
        intrEventTick = intrEvent->when();
    SERIALIZE_SCALAR(intrEventTick);

}

void
NSGigE::unserialize(Checkpoint *cp, const std::string &section)
{
    // Unserialize the PciDev base class
    PciDev::unserialize(cp, section);

    UNSERIALIZE_SCALAR(regs.command);
    UNSERIALIZE_SCALAR(regs.config);
    UNSERIALIZE_SCALAR(regs.mear);
    UNSERIALIZE_SCALAR(regs.ptscr);
    UNSERIALIZE_SCALAR(regs.isr);
    UNSERIALIZE_SCALAR(regs.imr);
    UNSERIALIZE_SCALAR(regs.ier);
    UNSERIALIZE_SCALAR(regs.ihr);
    UNSERIALIZE_SCALAR(regs.txdp);
    UNSERIALIZE_SCALAR(regs.txdp_hi);
    UNSERIALIZE_SCALAR(regs.txcfg);
    UNSERIALIZE_SCALAR(regs.gpior);
    UNSERIALIZE_SCALAR(regs.rxdp);
    UNSERIALIZE_SCALAR(regs.rxdp_hi);
    UNSERIALIZE_SCALAR(regs.rxcfg);
    UNSERIALIZE_SCALAR(regs.pqcr);
    UNSERIALIZE_SCALAR(regs.wcsr);
    UNSERIALIZE_SCALAR(regs.pcr);
    UNSERIALIZE_SCALAR(regs.rfcr);
    UNSERIALIZE_SCALAR(regs.rfdr);
    UNSERIALIZE_SCALAR(regs.srr);
    UNSERIALIZE_SCALAR(regs.mibc);
    UNSERIALIZE_SCALAR(regs.vrcr);
    UNSERIALIZE_SCALAR(regs.vtcr);
    UNSERIALIZE_SCALAR(regs.vdr);
    UNSERIALIZE_SCALAR(regs.ccsr);
    UNSERIALIZE_SCALAR(regs.tbicr);
    UNSERIALIZE_SCALAR(regs.tbisr);
    UNSERIALIZE_SCALAR(regs.tanar);
    UNSERIALIZE_SCALAR(regs.tanlpar);
    UNSERIALIZE_SCALAR(regs.taner);
    UNSERIALIZE_SCALAR(regs.tesr);

    UNSERIALIZE_ARRAY(rom.perfectMatch, ETH_ADDR_LEN);

    UNSERIALIZE_SCALAR(ioEnable);

    /*
     * unserialize the data fifos
     */
    rxFifo.unserialize("rxFifo", cp, section);
    txFifo.unserialize("txFifo", cp, section);

    /*
     * unserialize the various helper variables
     */
    bool txPacketExists;
    UNSERIALIZE_SCALAR(txPacketExists);
    if (txPacketExists) {
        txPacket = new PacketData(16384);
        txPacket->unserialize("txPacket", cp, section);
        uint32_t txPktBufPtr;
        UNSERIALIZE_SCALAR(txPktBufPtr);
        txPacketBufPtr = (uint8_t *) txPacket->data + txPktBufPtr;
    } else
        txPacket = 0;

    bool rxPacketExists;
    UNSERIALIZE_SCALAR(rxPacketExists);
    rxPacket = 0;
    if (rxPacketExists) {
        rxPacket = new PacketData(16384);
        rxPacket->unserialize("rxPacket", cp, section);
        uint32_t rxPktBufPtr;
        UNSERIALIZE_SCALAR(rxPktBufPtr);
        rxPacketBufPtr = (uint8_t *) rxPacket->data + rxPktBufPtr;
    } else
        rxPacket = 0;

    UNSERIALIZE_SCALAR(txXferLen);
    UNSERIALIZE_SCALAR(rxXferLen);

    /*
     * Unserialize DescCaches
     */
    UNSERIALIZE_SCALAR(txDescCache.link);
    UNSERIALIZE_SCALAR(txDescCache.bufptr);
    UNSERIALIZE_SCALAR(txDescCache.cmdsts);
    UNSERIALIZE_SCALAR(txDescCache.extsts);
    UNSERIALIZE_SCALAR(rxDescCache.link);
    UNSERIALIZE_SCALAR(rxDescCache.bufptr);
    UNSERIALIZE_SCALAR(rxDescCache.cmdsts);
    UNSERIALIZE_SCALAR(rxDescCache.extsts);

    /*
     * unserialize tx state machine
     */
    int txState;
    UNSERIALIZE_SCALAR(txState);
    this->txState = (TxState) txState;
    UNSERIALIZE_SCALAR(txEnable);
    UNSERIALIZE_SCALAR(CTDD);
    UNSERIALIZE_SCALAR(txFragPtr);
    UNSERIALIZE_SCALAR(txDescCnt);
    int txDmaState;
    UNSERIALIZE_SCALAR(txDmaState);
    this->txDmaState = (DmaState) txDmaState;

    /*
     * unserialize rx state machine
     */
    int rxState;
    UNSERIALIZE_SCALAR(rxState);
    this->rxState = (RxState) rxState;
    UNSERIALIZE_SCALAR(rxEnable);
    UNSERIALIZE_SCALAR(CRDD);
    UNSERIALIZE_SCALAR(rxPktBytes);
    UNSERIALIZE_SCALAR(rxFragPtr);
    UNSERIALIZE_SCALAR(rxDescCnt);
    int rxDmaState;
    UNSERIALIZE_SCALAR(rxDmaState);
    this->rxDmaState = (DmaState) rxDmaState;

    UNSERIALIZE_SCALAR(extstsEnable);

     /*
     * If there's a pending transmit, reschedule it now
     */
    Tick transmitTick;
    UNSERIALIZE_SCALAR(transmitTick);
    if (transmitTick)
        txEvent.schedule(curTick + transmitTick);

    /*
     * unserialize receive address filter settings
     */
    UNSERIALIZE_SCALAR(rxFilterEnable);
    UNSERIALIZE_SCALAR(acceptBroadcast);
    UNSERIALIZE_SCALAR(acceptMulticast);
    UNSERIALIZE_SCALAR(acceptUnicast);
    UNSERIALIZE_SCALAR(acceptPerfect);
    UNSERIALIZE_SCALAR(acceptArp);

    /*
     * Keep track of pending interrupt status.
     */
    UNSERIALIZE_SCALAR(intrTick);
    UNSERIALIZE_SCALAR(cpuPendingIntr);
    Tick intrEventTick;
    UNSERIALIZE_SCALAR(intrEventTick);
    if (intrEventTick) {
        intrEvent = new IntrEvent(this, true);
        intrEvent->schedule(intrEventTick);
    }

    /*
     * re-add addrRanges to bus bridges
     */
    if (pioInterface) {
        pioInterface->addAddrRange(RangeSize(BARAddrs[0], BARSize[0]));
        pioInterface->addAddrRange(RangeSize(BARAddrs[1], BARSize[1]));
    }
}

Tick
NSGigE::cacheAccess(MemReqPtr &req)
{
    DPRINTF(EthernetPIO, "timing access to paddr=%#x (daddr=%#x)\n",
            req->paddr, req->paddr - addr);
    return curTick + pioLatency;
}

BEGIN_DECLARE_SIM_OBJECT_PARAMS(NSGigEInt)

    SimObjectParam<EtherInt *> peer;
    SimObjectParam<NSGigE *> device;

END_DECLARE_SIM_OBJECT_PARAMS(NSGigEInt)

BEGIN_INIT_SIM_OBJECT_PARAMS(NSGigEInt)

    INIT_PARAM_DFLT(peer, "peer interface", NULL),
    INIT_PARAM(device, "Ethernet device of this interface")

END_INIT_SIM_OBJECT_PARAMS(NSGigEInt)

CREATE_SIM_OBJECT(NSGigEInt)
{
    NSGigEInt *dev_int = new NSGigEInt(getInstanceName(), device);

    EtherInt *p = (EtherInt *)peer;
    if (p) {
        dev_int->setPeer(p);
        p->setPeer(dev_int);
    }

    return dev_int;
}

REGISTER_SIM_OBJECT("NSGigEInt", NSGigEInt)


BEGIN_DECLARE_SIM_OBJECT_PARAMS(NSGigE)

    Param<Tick> tx_delay;
    Param<Tick> rx_delay;
    Param<Tick> intr_delay;
    SimObjectParam<MemoryController *> mmu;
    SimObjectParam<PhysicalMemory *> physmem;
    Param<bool> rx_filter;
    Param<string> hardware_address;
    SimObjectParam<Bus*> header_bus;
    SimObjectParam<Bus*> payload_bus;
    SimObjectParam<HierParams *> hier;
    Param<Tick> pio_latency;
    Param<bool> dma_desc_free;
    Param<bool> dma_data_free;
    Param<Tick> dma_read_delay;
    Param<Tick> dma_write_delay;
    Param<Tick> dma_read_factor;
    Param<Tick> dma_write_factor;
    SimObjectParam<PciConfigAll *> configspace;
    SimObjectParam<PciConfigData *> configdata;
    SimObjectParam<Platform *> platform;
    Param<uint32_t> pci_bus;
    Param<uint32_t> pci_dev;
    Param<uint32_t> pci_func;
    Param<uint32_t> tx_fifo_size;
    Param<uint32_t> rx_fifo_size;

END_DECLARE_SIM_OBJECT_PARAMS(NSGigE)

BEGIN_INIT_SIM_OBJECT_PARAMS(NSGigE)

    INIT_PARAM_DFLT(tx_delay, "Transmit Delay", 1000),
    INIT_PARAM_DFLT(rx_delay, "Receive Delay", 1000),
    INIT_PARAM_DFLT(intr_delay, "Interrupt Delay in microseconds", 0),
    INIT_PARAM(mmu, "Memory Controller"),
    INIT_PARAM(physmem, "Physical Memory"),
    INIT_PARAM_DFLT(rx_filter, "Enable Receive Filter", true),
    INIT_PARAM_DFLT(hardware_address, "Ethernet Hardware Address",
                    "00:99:00:00:00:01"),
    INIT_PARAM_DFLT(header_bus, "The IO Bus to attach to for headers", NULL),
    INIT_PARAM_DFLT(payload_bus, "The IO Bus to attach to for payload", NULL),
    INIT_PARAM_DFLT(hier, "Hierarchy global variables", &defaultHierParams),
    INIT_PARAM_DFLT(pio_latency, "Programmed IO latency in bus cycles", 1),
    INIT_PARAM_DFLT(dma_desc_free, "DMA of Descriptors is free", false),
    INIT_PARAM_DFLT(dma_data_free, "DMA of Data is free", false),
    INIT_PARAM_DFLT(dma_read_delay, "fixed delay for dma reads", 0),
    INIT_PARAM_DFLT(dma_write_delay, "fixed delay for dma writes", 0),
    INIT_PARAM_DFLT(dma_read_factor, "multiplier for dma reads", 0),
    INIT_PARAM_DFLT(dma_write_factor, "multiplier for dma writes", 0),
    INIT_PARAM(configspace, "PCI Configspace"),
    INIT_PARAM(configdata, "PCI Config data"),
    INIT_PARAM(platform, "Platform"),
    INIT_PARAM(pci_bus, "PCI bus"),
    INIT_PARAM(pci_dev, "PCI device number"),
    INIT_PARAM(pci_func, "PCI function code"),
    INIT_PARAM_DFLT(tx_fifo_size, "max size in bytes of txFifo", 131072),
    INIT_PARAM_DFLT(rx_fifo_size, "max size in bytes of rxFifo", 131072)

END_INIT_SIM_OBJECT_PARAMS(NSGigE)


CREATE_SIM_OBJECT(NSGigE)
{
    NSGigE::Params *params = new NSGigE::Params;

    params->name = getInstanceName();
    params->mmu = mmu;
    params->configSpace = configspace;
    params->configData = configdata;
    params->plat = platform;
    params->busNum = pci_bus;
    params->deviceNum = pci_dev;
    params->functionNum = pci_func;

    params->intr_delay = intr_delay;
    params->pmem = physmem;
    params->tx_delay = tx_delay;
    params->rx_delay = rx_delay;
    params->hier = hier;
    params->header_bus = header_bus;
    params->payload_bus = payload_bus;
    params->pio_latency = pio_latency;
    params->dma_desc_free = dma_desc_free;
    params->dma_data_free = dma_data_free;
    params->dma_read_delay = dma_read_delay;
    params->dma_write_delay = dma_write_delay;
    params->dma_read_factor = dma_read_factor;
    params->dma_write_factor = dma_write_factor;
    params->rx_filter = rx_filter;
    params->eaddr = hardware_address;
    params->tx_fifo_size = tx_fifo_size;
    params->rx_fifo_size = rx_fifo_size;
    return new NSGigE(params);
}

REGISTER_SIM_OBJECT("NSGigE", NSGigE)
