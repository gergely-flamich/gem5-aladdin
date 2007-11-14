/*
 * Copyright N) 2007 MIPS Technologies, Inc.  All Rights Reserved
 *
 * This software is part of the M5 simulator.
 *
 * THIS IS A LEGAL AGREEMENT.  BY DOWNLOADING, USING, COPYING, CREATING
 * DERIVATIVE WORKS, AND/OR DISTRIBUTING THIS SOFTWARE YOU ARE AGREEING
 * TO THESE TERMS AND CONDITIONS.
 *
 * Permission is granted to use, copy, create derivative works and
 * distribute this software and such derivative works for any purpose,
 * so long as (1) the copyright notice above, this grant of permission,
 * and the disclaimer below appear in all copies and derivative works
 * made, (2) the copyright notice above is augmented as appropriate to
 * reflect the addition of any new copyrightable work in a derivative
 * work (e.g., Copyright N) <Publication Year> Copyright Owner), and (3)
 * the name of MIPS Technologies, Inc. ($(B!H(BMIPS$(B!I(B) is not used in any
 * advertising or publicity pertaining to the use or distribution of
 * this software without specific, written prior authorization.
 *
 * THIS SOFTWARE IS PROVIDED $(B!H(BAS IS.$(B!I(B  MIPS MAKES NO WARRANTIES AND
 * DISCLAIMS ALL WARRANTIES, WHETHER EXPRESS, STATUTORY, IMPLIED OR
 * OTHERWISE, INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
 * NON-INFRINGEMENT OF THIRD PARTY RIGHTS, REGARDING THIS SOFTWARE.
 * IN NO EVENT SHALL MIPS BE LIABLE FOR ANY DAMAGES, INCLUDING DIRECT,
 * INDIRECT, INCIDENTAL, CONSEQUENTIAL, SPECIAL, OR PUNITIVE DAMAGES OF
 * ANY KIND OR NATURE, ARISING OUT OF OR IN CONNECTION WITH THIS AGREEMENT,
 * THIS SOFTWARE AND/OR THE USE OF THIS SOFTWARE, WHETHER SUCH LIABILITY
 * IS ASSERTED ON THE BASIS OF CONTRACT, TORT (INCLUDING NEGLIGENCE OR
 * STRICT LIABILITY), OR OTHERWISE, EVEN IF MIPS HAS BEEN WARNED OF THE
 * POSSIBILITY OF ANY SUCH LOSS OR DAMAGE IN ADVANCE.
 *
 * Authors: Gabe M. Black
 *          Korey L. Sewell
 *          Jaidev Patwardhan
 */

#include "arch/mips/faults.hh"
#include "cpu/thread_context.hh"
#include "cpu/base.hh"
#include "base/trace.hh"
#include "arch/mips/pra_constants.hh"
#if !FULL_SYSTEM
#include "sim/process.hh"
#include "mem/page_table.hh"
#endif

namespace MipsISA
{

FaultName MachineCheckFault::_name = "Machine Check";
FaultVect MachineCheckFault::_vect = 0x0401;
FaultStat MachineCheckFault::_count;

FaultName AlignmentFault::_name = "Alignment";
FaultVect AlignmentFault::_vect = 0x0301;
FaultStat AlignmentFault::_count;

FaultName ResetFault::_name = "Reset Fault";
#if  FULL_SYSTEM
FaultVect ResetFault::_vect = 0xBFC00000;
#else
FaultVect ResetFault::_vect = 0x001;
#endif
FaultStat ResetFault::_count;

FaultName AddressErrorFault::_name = "Address Error";
FaultVect AddressErrorFault::_vect = 0x0180;
FaultStat AddressErrorFault::_count;

FaultName StoreAddressErrorFault::_name = "Store Address Error";
FaultVect StoreAddressErrorFault::_vect = 0x0180;
FaultStat StoreAddressErrorFault::_count;


FaultName SystemCallFault::_name = "Syscall";
FaultVect SystemCallFault::_vect = 0x0180;
FaultStat SystemCallFault::_count;

FaultName CoprocessorUnusableFault::_name = "Coprocessor Unusable Fault";
FaultVect CoprocessorUnusableFault::_vect = 0x180;
FaultStat CoprocessorUnusableFault::_count;

FaultName ReservedInstructionFault::_name = "Reserved Instruction Fault";
FaultVect ReservedInstructionFault::_vect = 0x0180;
FaultStat ReservedInstructionFault::_count;

FaultName ThreadFault::_name = "Thread Fault";
FaultVect ThreadFault::_vect = 0x00F1;
FaultStat ThreadFault::_count;


FaultName ArithmeticFault::_name = "Arithmetic Overflow Exception";
FaultVect ArithmeticFault::_vect = 0x180;
FaultStat ArithmeticFault::_count;

FaultName UnimplementedOpcodeFault::_name = "opdec";
FaultVect UnimplementedOpcodeFault::_vect = 0x0481;
FaultStat UnimplementedOpcodeFault::_count;

FaultName InterruptFault::_name = "interrupt";
FaultVect InterruptFault::_vect = 0x0180;
FaultStat InterruptFault::_count;

FaultName TrapFault::_name = "Trap";
FaultVect TrapFault::_vect = 0x0180;
FaultStat TrapFault::_count;

FaultName BreakpointFault::_name = "Breakpoint";
FaultVect BreakpointFault::_vect = 0x0180;
FaultStat BreakpointFault::_count;


FaultName ItbInvalidFault::_name = "Invalid TLB Entry Exception (I-Fetch/LW)";
FaultVect ItbInvalidFault::_vect = 0x0180;
FaultStat ItbInvalidFault::_count;

FaultName ItbPageFault::_name = "itbmiss";
FaultVect ItbPageFault::_vect = 0x0181;
FaultStat ItbPageFault::_count;

FaultName ItbMissFault::_name = "itbmiss";
FaultVect ItbMissFault::_vect = 0x0181;
FaultStat ItbMissFault::_count;

FaultName ItbAcvFault::_name = "iaccvio";
FaultVect ItbAcvFault::_vect = 0x0081;
FaultStat ItbAcvFault::_count;

FaultName ItbRefillFault::_name = "TLB Refill Exception (I-Fetch/LW)";
FaultVect ItbRefillFault::_vect = 0x0180;
FaultStat ItbRefillFault::_count;

FaultName NDtbMissFault::_name = "dtb_miss_single";
FaultVect NDtbMissFault::_vect = 0x0201;
FaultStat NDtbMissFault::_count;

FaultName PDtbMissFault::_name = "dtb_miss_double";
FaultVect PDtbMissFault::_vect = 0x0281;
FaultStat PDtbMissFault::_count;

FaultName DtbPageFault::_name = "dfault";
FaultVect DtbPageFault::_vect = 0x0381;
FaultStat DtbPageFault::_count;

FaultName DtbAcvFault::_name = "dfault";
FaultVect DtbAcvFault::_vect = 0x0381;
FaultStat DtbAcvFault::_count;

FaultName DtbInvalidFault::_name = "Invalid TLB Entry Exception (Store)";
FaultVect DtbInvalidFault::_vect = 0x0180;
FaultStat DtbInvalidFault::_count;

FaultName DtbRefillFault::_name = "TLB Refill Exception (Store)";
FaultVect DtbRefillFault::_vect = 0x0180;
FaultStat DtbRefillFault::_count;

FaultName TLBModifiedFault::_name = "TLB Modified Exception";
FaultVect TLBModifiedFault::_vect = 0x0180;
FaultStat TLBModifiedFault::_count;

FaultName FloatEnableFault::_name = "float_enable_fault";
FaultVect FloatEnableFault::_vect = 0x0581;
FaultStat FloatEnableFault::_count;

FaultName IntegerOverflowFault::_name = "Integer Overflow Fault";
FaultVect IntegerOverflowFault::_vect = 0x0501;
FaultStat IntegerOverflowFault::_count;

FaultName DspStateDisabledFault::_name = "DSP Disabled Fault";
FaultVect DspStateDisabledFault::_vect = 0x001a;
FaultStat DspStateDisabledFault::_count;

#if FULL_SYSTEM
void MipsFault::setHandlerPC(Addr HandlerBase, ThreadContext *tc)
{
  tc->setPC(HandlerBase);
  tc->setNextPC(HandlerBase+sizeof(MachInst));
  tc->setNextNPC(HandlerBase+2*sizeof(MachInst));
}

void MipsFault::setExceptionState(ThreadContext *tc,uint8_t ExcCode)
{
  // modify SRS Ctl - Save CSS, put ESS into CSS
  MiscReg stat = tc->readMiscReg(MipsISA::Status);
  if(bits(stat,Status_EXL) != 1 && bits(stat,Status_BEV) != 1)
    {
      // SRS Ctl is modified only if Status_EXL and Status_BEV are not set
      MiscReg srs = tc->readMiscReg(MipsISA::SRSCtl);
      uint8_t CSS,ESS;
      CSS = bits(srs,SRSCtl_CSS_HI,SRSCtl_CSS_LO);
      ESS = bits(srs,SRSCtl_ESS_HI,SRSCtl_ESS_LO);
      // Move CSS to PSS
      replaceBits(srs,SRSCtl_PSS_HI,SRSCtl_PSS_LO,CSS);
      // Move ESS to CSS
      replaceBits(srs,SRSCtl_CSS_HI,SRSCtl_CSS_LO,ESS);
      tc->setMiscRegNoEffect(MipsISA::SRSCtl,srs);
      tc->setShadowSet(ESS);
    }

  // set EXL bit (don't care if it is already set!)
  replaceBits(stat,Status_EXL_HI,Status_EXL_LO,1);
  tc->setMiscRegNoEffect(MipsISA::Status,stat);

  // write EPC
  //  warn("Set EPC to %x\n",tc->readPC());
  // CHECK ME  or FIXME or FIX ME or POSSIBLE HACK
  // Check to see if the exception occurred in the branch delay slot
  DPRINTF(MipsPRA,"PC: %x, NextPC: %x, NNPC: %x\n",tc->readPC(),tc->readNextPC(),tc->readNextNPC());
  int C_BD=0;
  if(tc->readPC() + sizeof(MachInst) != tc->readNextPC()){
    tc->setMiscRegNoEffect(MipsISA::EPC,tc->readPC()-sizeof(MachInst));
    // In the branch delay slot? set CAUSE_31
    C_BD = 1;
  } else {
    tc->setMiscRegNoEffect(MipsISA::EPC,tc->readPC());
    // In the branch delay slot? reset CAUSE_31
    C_BD = 0;
  }

  // Set Cause_EXCCODE field
  MiscReg cause = tc->readMiscReg(MipsISA::Cause);
  replaceBits(cause,Cause_EXCCODE_HI,Cause_EXCCODE_LO,ExcCode);
  replaceBits(cause,Cause_BD_HI,Cause_BD_LO,C_BD);
  replaceBits(cause,Cause_CE_HI,Cause_CE_LO,0);
  tc->setMiscRegNoEffect(MipsISA::Cause,cause);

}

void ArithmeticFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  setExceptionState(tc,0xC);

  // Set new PC
  Addr HandlerBase;
  MiscReg stat = tc->readMiscReg(MipsISA::Status);
  // Here, the handler is dependent on BEV, which is not modified by setExceptionState()
  if(bits(stat,Status_BEV)==0){ // See MIPS ARM Vol 3, Revision 2, Page 38
    HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase);
  }else{
    HandlerBase = 0xBFC00200;
  }
  setHandlerPC(HandlerBase,tc);
  //      warn("Exception Handler At: %x \n",HandlerBase);
}

void StoreAddressErrorFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  setExceptionState(tc,0x5);
  tc->setMiscRegNoEffect(MipsISA::BadVAddr,BadVAddr);

  // Set new PC
  Addr HandlerBase;
  HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
  setHandlerPC(HandlerBase,tc);
  //      warn("Exception Handler At: %x \n",HandlerBase);
  //      warn("Exception Handler At: %x , EPC set to %x\n",HandlerBase,tc->readMiscReg(MipsISA::EPC));

}

void TrapFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  //  warn("%s encountered.\n", name());
  setExceptionState(tc,0xD);

  // Set new PC
  Addr HandlerBase;
  HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
  setHandlerPC(HandlerBase,tc);
  //      warn("Exception Handler At: %x \n",HandlerBase);
  //      warn("Exception Handler At: %x , EPC set to %x\n",HandlerBase,tc->readMiscReg(MipsISA::EPC));
}

void BreakpointFault::invoke(ThreadContext *tc)
{
      setExceptionState(tc,0x9);

      // Set new PC
      Addr HandlerBase;
      HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
      setHandlerPC(HandlerBase,tc);
      //      warn("Exception Handler At: %x \n",HandlerBase);
      //      warn("Exception Handler At: %x , EPC set to %x\n",HandlerBase,tc->readMiscReg(MipsISA::EPC));

}

void DtbInvalidFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  //    warn("%s encountered.\n", name());
  tc->setMiscRegNoEffect(MipsISA::BadVAddr,BadVAddr);
  MiscReg eh = tc->readMiscReg(MipsISA::EntryHi);
  replaceBits(eh,EntryHi_ASID_HI,EntryHi_ASID_LO,EntryHi_Asid);
  replaceBits(eh,EntryHi_VPN2_HI,EntryHi_VPN2_LO,EntryHi_VPN2);
  replaceBits(eh,EntryHi_VPN2X_HI,EntryHi_VPN2X_LO,EntryHi_VPN2X);
  tc->setMiscRegNoEffect(MipsISA::EntryHi,eh);
  MiscReg ctxt = tc->readMiscReg(MipsISA::Context);
  replaceBits(ctxt,Context_BadVPN2_HI,Context_BadVPN2_LO,Context_BadVPN2);
  tc->setMiscRegNoEffect(MipsISA::Context,ctxt);
  setExceptionState(tc,0x3);


  // Set new PC
  Addr HandlerBase;
  HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
  setHandlerPC(HandlerBase,tc);
  //      warn("Exception Handler At: %x , EPC set to %x\n",HandlerBase,tc->readMiscReg(MipsISA::EPC));
}

void AddressErrorFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
      setExceptionState(tc,0x4);
      tc->setMiscRegNoEffect(MipsISA::BadVAddr,BadVAddr);

      // Set new PC
      Addr HandlerBase;
      HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
      setHandlerPC(HandlerBase,tc);
}

void ItbInvalidFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
      setExceptionState(tc,0x2);
      tc->setMiscRegNoEffect(MipsISA::BadVAddr,BadVAddr);
      MiscReg eh = tc->readMiscReg(MipsISA::EntryHi);
      replaceBits(eh,EntryHi_ASID_HI,EntryHi_ASID_LO,EntryHi_Asid);
      replaceBits(eh,EntryHi_VPN2_HI,EntryHi_VPN2_LO,EntryHi_VPN2);
      replaceBits(eh,EntryHi_VPN2X_HI,EntryHi_VPN2X_LO,EntryHi_VPN2X);
      tc->setMiscRegNoEffect(MipsISA::EntryHi,eh);
      MiscReg ctxt = tc->readMiscReg(MipsISA::Context);
      replaceBits(ctxt,Context_BadVPN2_HI,Context_BadVPN2_LO,Context_BadVPN2);
      tc->setMiscRegNoEffect(MipsISA::Context,ctxt);


      // Set new PC
      Addr HandlerBase;
      HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
      setHandlerPC(HandlerBase,tc);
      DPRINTF(MipsPRA,"Exception Handler At: %x , EPC set to %x\n",HandlerBase,tc->readMiscReg(MipsISA::EPC));
}

void ItbRefillFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered (%x).\n", name(),BadVAddr);
  Addr HandlerBase;
  tc->setMiscRegNoEffect(MipsISA::BadVAddr,BadVAddr);
  MiscReg eh = tc->readMiscReg(MipsISA::EntryHi);
  replaceBits(eh,EntryHi_ASID_HI,EntryHi_ASID_LO,EntryHi_Asid);
  replaceBits(eh,EntryHi_VPN2_HI,EntryHi_VPN2_LO,EntryHi_VPN2);
  replaceBits(eh,EntryHi_VPN2X_HI,EntryHi_VPN2X_LO,EntryHi_VPN2X);
  tc->setMiscRegNoEffect(MipsISA::EntryHi,eh);
  MiscReg ctxt = tc->readMiscReg(MipsISA::Context);
  replaceBits(ctxt,Context_BadVPN2_HI,Context_BadVPN2_LO,Context_BadVPN2);
  tc->setMiscRegNoEffect(MipsISA::Context,ctxt);

  MiscReg stat = tc->readMiscReg(MipsISA::Status);
  // Since handler depends on EXL bit, must check EXL bit before setting it!!
  if(bits(stat,Status_EXL)==1){ // See MIPS ARM Vol 3, Revision 2, Page 38
    HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
  }else{
    HandlerBase = tc->readMiscReg(MipsISA::EBase); // Offset 0x000
  }

  setExceptionState(tc,0x2);
  setHandlerPC(HandlerBase,tc);
}

void DtbRefillFault::invoke(ThreadContext *tc)
{
  // Set new PC
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  Addr HandlerBase;
  tc->setMiscRegNoEffect(MipsISA::BadVAddr,BadVAddr);
  MiscReg eh = tc->readMiscReg(MipsISA::EntryHi);
  replaceBits(eh,EntryHi_ASID_HI,EntryHi_ASID_LO,EntryHi_Asid);
  replaceBits(eh,EntryHi_VPN2_HI,EntryHi_VPN2_LO,EntryHi_VPN2);
  replaceBits(eh,EntryHi_VPN2X_HI,EntryHi_VPN2X_LO,EntryHi_VPN2X);
  tc->setMiscRegNoEffect(MipsISA::EntryHi,eh);
  MiscReg ctxt = tc->readMiscReg(MipsISA::Context);
  replaceBits(ctxt,Context_BadVPN2_HI,Context_BadVPN2_LO,Context_BadVPN2);
  tc->setMiscRegNoEffect(MipsISA::Context,ctxt);

  MiscReg stat = tc->readMiscReg(MipsISA::Status);
  // Since handler depends on EXL bit, must check EXL bit before setting it!!
  if(bits(stat,Status_EXL)==1){ // See MIPS ARM Vol 3, Revision 2, Page 38
    HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
  }else{
    HandlerBase = tc->readMiscReg(MipsISA::EBase); // Offset 0x000
  }


  setExceptionState(tc,0x3);

  setHandlerPC(HandlerBase,tc);
}

void TLBModifiedFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  tc->setMiscRegNoEffect(MipsISA::BadVAddr,BadVAddr);
  MiscReg eh = tc->readMiscReg(MipsISA::EntryHi);
  replaceBits(eh,EntryHi_ASID_HI,EntryHi_ASID_LO,EntryHi_Asid);
  replaceBits(eh,EntryHi_VPN2_HI,EntryHi_VPN2_LO,EntryHi_VPN2);
  replaceBits(eh,EntryHi_VPN2X_HI,EntryHi_VPN2X_LO,EntryHi_VPN2X);
  tc->setMiscRegNoEffect(MipsISA::EntryHi,eh);
  MiscReg ctxt = tc->readMiscReg(MipsISA::Context);
  replaceBits(ctxt,Context_BadVPN2_HI,Context_BadVPN2_LO,Context_BadVPN2);
  tc->setMiscRegNoEffect(MipsISA::Context,ctxt);

    // Set new PC
      Addr HandlerBase;
      HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
      setExceptionState(tc,0x1);
      setHandlerPC(HandlerBase,tc);
      //      warn("Exception Handler At: %x , EPC set to %x\n",HandlerBase,tc->readMiscReg(MipsISA::EPC));

}

void SystemCallFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
      setExceptionState(tc,0x8);

      // Set new PC
      Addr HandlerBase;
      HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
      setHandlerPC(HandlerBase,tc);
      //      warn("Exception Handler At: %x \n",HandlerBase);
      //      warn("Exception Handler At: %x , EPC set to %x\n",HandlerBase,tc->readMiscReg(MipsISA::EPC));

}

void InterruptFault::invoke(ThreadContext *tc)
{
#if  FULL_SYSTEM
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  //RegFile *Reg = tc->getRegFilePtr(); // Get pointer to the register fil
  setExceptionState(tc,0x0A);
  Addr HandlerBase;


  uint8_t IV = bits(tc->readMiscRegNoEffect(MipsISA::Cause),Cause_IV);
  if (IV)// Offset 200 for release 2
      HandlerBase= 0x20 + vect() + tc->readMiscRegNoEffect(MipsISA::EBase);
  else//Ofset at 180 for release 1
      HandlerBase= vect() + tc->readMiscRegNoEffect(MipsISA::EBase);

  setHandlerPC(HandlerBase,tc);
#endif
}

#endif // FULL_SYSTEM

void ResetFault::invoke(ThreadContext *tc)
{
#if FULL_SYSTEM
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  /* All reset activity must be invoked from here */
  tc->setPC(vect());
  tc->setNextPC(vect()+sizeof(MachInst));
  tc->setNextNPC(vect()+sizeof(MachInst)+sizeof(MachInst));
  DPRINTF(MipsPRA,"(%x)  -  ResetFault::invoke : PC set to %x",(unsigned)tc,(unsigned)tc->readPC());
#endif

  // Set Coprocessor 1 (Floating Point) To Usable
  tc->setMiscReg(MipsISA::Status, MipsISA::Status | 0x20000000);
}

void ReservedInstructionFault::invoke(ThreadContext *tc)
{
#if  FULL_SYSTEM
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  //RegFile *Reg = tc->getRegFilePtr(); // Get pointer to the register fil
  setExceptionState(tc,0x0A);
  Addr HandlerBase;
  HandlerBase= vect() + tc->readMiscRegNoEffect(MipsISA::EBase); // Offset 0x180 - General Exception Vector
  setHandlerPC(HandlerBase,tc);
#else
    panic("%s encountered.\n", name());
#endif
}

void ThreadFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  panic("%s encountered.\n", name());
}

void DspStateDisabledFault::invoke(ThreadContext *tc)
{
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  panic("%s encountered.\n", name());
}

void CoprocessorUnusableFault::invoke(ThreadContext *tc)
{
#if FULL_SYSTEM
  DPRINTF(MipsPRA,"%s encountered.\n", name());
  setExceptionState(tc,0xb);
  /* The ID of the coprocessor causing the exception is stored in CoprocessorUnusableFault::coProcID */
  MiscReg cause = tc->readMiscReg(MipsISA::Cause);
  replaceBits(cause,Cause_CE_HI,Cause_CE_LO,coProcID);
  tc->setMiscRegNoEffect(MipsISA::Cause,cause);

  Addr HandlerBase;
  HandlerBase= vect() + tc->readMiscReg(MipsISA::EBase); // Offset 0x180 - General Exception Vector
  setHandlerPC(HandlerBase,tc);

  //      warn("Status: %x, Cause: %x\n",tc->readMiscReg(MipsISA::Status),tc->readMiscReg(MipsISA::Cause));
#else
    warn("%s (CP%d) encountered.\n", name(), coProcID);
#endif
}

} // namespace MipsISA

