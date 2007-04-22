/*
 * Dibbler - a portable DHCPv6
 *
 * authors: Tomasz Mrugalski <thomson@klub.com.pl>
 *          Marek Senderski <msend@o2.pl>
 * changes: Krzysztof Wnuk <keczi@poczta.onet.pl>
 *          Michal Kowalczuk <michal@kowalczuk.eu>
 *
 * released under GNU GPL v2 or later licence
 *
 * $Id: ClntMsgRequest.cpp,v 1.22 2007-04-22 23:03:18 thomson Exp $
 *
 */

#include "ClntMsgRequest.h"
#include "SmartPtr.h"
#include "DHCPConst.h"
#include "ClntIfaceMgr.h"
#include "ClntMsgAdvertise.h"
#include "ClntOptServerIdentifier.h"
#include "ClntOptServerUnicast.h"
#include "ClntOptIA_NA.h"
#include "ClntOptTA.h"
#include "ClntOptIA_PD.h"
#include "ClntOptElapsed.h"
#include "ClntOptClientIdentifier.h"
#include "ClntOptOptionRequest.h"
#include <cmath>
#include "Logger.h"

/*
 * opts - options list WITHOUT serverDUID
 */
TClntMsgRequest::TClntMsgRequest(SmartPtr<TClntIfaceMgr> IfaceMgr, 
				 SmartPtr<TClntTransMgr> TransMgr,
				 SmartPtr<TClntCfgMgr>   CfgMgr, 
				 SmartPtr<TClntAddrMgr> AddrMgr, 
				 List(TOpt) opts, 
				 
				 int iface)
    :TClntMsg(IfaceMgr,TransMgr,CfgMgr,AddrMgr,iface, SmartPtr<TIPv6Addr>() /*NULL*/, REQUEST_MSG) {
    IRT = REQ_TIMEOUT;
    MRT = REQ_MAX_RT;
    MRC = REQ_MAX_RC;
    MRD = 0;   
    RT=0;

    Iface=iface;
    IsDone=false;

    int backupCount = TransMgr->getAdvertiseLstCount();
    if (!backupCount) 
    {
	Log(Error) << "Unable to send REQUEST. There are no backup servers left." << LogEnd;
	setState( opts, STATE_NOTCONFIGURED);

        this->IsDone = true;
        return;
    }
    Log(Info) << "Creating REQUEST. Backup server list contains " 
	      << backupCount << " server(s)." << LogEnd;
    TransMgr->printAdvertiseLst();

    // get server DUID from the first advertise
    SPtr<TOpt> srvDUID = TransMgr->getAdvertiseDUID();

    TransMgr->firstAdvertise();
    SPtr<TClntMsgAdvertise> advertise = (Ptr*) TransMgr->getAdvertise();

    // remove just used server
    TransMgr->delFirstAdvertise();

    // copy whole list from SOLICIT ...
    Options = opts;
    // set proper Parent in copied options
    Options.first();
    SmartPtr<TOpt> opt;
    while (opt = Options.get())
        opt->setParent(this);

    // copy addresses offered in ADVERTISE
    copyAddrsFromAdvertise((Ptr*) advertise);

    // does this server support unicast?
    SmartPtr<TClntCfgIface> cfgIface = CfgMgr->getIface(iface);
    if (!cfgIface)
	Log(Error) << "Unable to find interface with ifindex " << iface << "." << LogEnd;    
    SmartPtr<TClntOptServerUnicast> unicast = (Ptr*) advertise->getOption(OPTION_UNICAST);
    if (unicast && !cfgIface->getUnicast()) {
	Log(Info) << "Server offers unicast (" << *unicast->getAddr() 
		  << ") communication, but this client is not configured to so." << LogEnd;
    }
    if (unicast && cfgIface->getUnicast()) {
	Log(Debug) << "Server supports unicast on address " << *unicast->getAddr() << "." << LogEnd;
	this->PeerAddr = unicast->getAddr();
	Options.first();
	SmartPtr<TOpt> opt;
	// set this unicast address in each IA in AddrMgr
	while (opt = Options.get()) {
	    if (opt->getOptType()!=OPTION_IA_NA)
		continue;
	    SmartPtr<TClntOptIA_NA> ptrOptIA = (Ptr*) opt;
	    SmartPtr<TAddrIA> ptrAddrIA = AddrMgr->getIA(ptrOptIA->getIAID());
	  
	    if (!ptrAddrIA) {
		Log(Crit) << "IA with IAID=" << ptrOptIA->getIAID() << " not found." << LogEnd;
		continue;
	    }
	    ptrAddrIA->setUnicast(unicast->getAddr());
	}
	
    }
    
    // ... and append server's DUID from ADVERTISE
    Options.append( srvDUID );
    
    pkt = new char[getSize()];

}

TClntMsgRequest::TClntMsgRequest(SmartPtr<TClntIfaceMgr> IfaceMgr, 
				 SmartPtr<TClntTransMgr> TransMgr,
				 SmartPtr<TClntCfgMgr>   CfgMgr, 
				 SmartPtr<TClntAddrMgr> AddrMgr, 
				 TContainer<SmartPtr<TAddrIA> > IAs,
				 SmartPtr<TDUID> srvDUID,
				 int iface)
    :TClntMsg(IfaceMgr,TransMgr,CfgMgr,AddrMgr,iface,SmartPtr<TIPv6Addr>()/*NULL*/,REQUEST_MSG) {
    IRT = REQ_TIMEOUT;
    MRT = REQ_MAX_RT;
    MRC = REQ_MAX_RC;
    MRD = 0;   
    RT=0;

    Iface=iface;
    IsDone=false;
    SmartPtr<TOpt> ptr;
    ptr = new TClntOptClientIdentifier( CfgMgr->getDUID(), this );
    Options.append( ptr );

    if (!srvDUID) {
	Log(Error) << "Unable to send REQUEST: ServerId not specified.\n" << LogEnd;
	IsDone = true;
	return;
    }
    
    ptr = (Ptr*) SmartPtr<TClntOptServerIdentifier> (new TClntOptServerIdentifier(srvDUID,this));
    // all IAs provided by checkSolicit
    SmartPtr<TAddrIA> ClntAddrIA;
    Options.append( ptr );
	
    IAs.first();
    while (ClntAddrIA = IAs.get()) 
    {
        SmartPtr<TClntCfgIA> ClntCfgIA = 
            ClntCfgMgr->getIA(ClntAddrIA->getIAID());
	SmartPtr<TClntOptIA_NA> IA_NA =
	    new TClntOptIA_NA(ClntCfgIA, ClntAddrIA, this);
	Options.append((Ptr*)IA_NA);
    }
    Options.append(new TClntOptElapsed(this));
    pkt = new char[getSize()];
}

/*
 * analyse REPLY received for this REQUEST
 */
void TClntMsgRequest::answer(SmartPtr<TClntMsg> msg)
{
    TClntMsg::answer(msg);
}

void TClntMsgRequest::doDuties()
{
    // timeout is reached and we still don't have answer, retransmit
    if (RC>MRC) 
    {
	ClntTransMgr->sendRequest(Options, Iface);

	IsDone = true;
	return;
    }
    send();
    return;
}

bool TClntMsgRequest::check()
{
    return false;
}

string TClntMsgRequest::getName() {
    return "REQUEST";
}

/** 
 * method is used when REQUEST transmission was attempted, but there are no more servers
 * on the backup list. When this method is called, it is sure that some IAs, TA or PDs will
 * remain not configured (as there are no servers available, which could configure it)
 * 
 * @param List 
 */
void TClntMsgRequest::setState(List(TOpt) opts, EState state)
{
    SPtr<TOpt>          opt;
    SPtr<TClntOptIA_NA> ia;
    SPtr<TClntOptTA>    ta;
    SPtr<TClntOptIA_PD> pd;

    SPtr<TClntCfgIA> cfgIa;
    SPtr<TClntCfgTA> cfgTa;
    SPtr<TClntCfgPD> cfgPd;

    SPtr<TClntCfgIface> iface = ClntCfgMgr->getIface(Iface);
    if (!iface) {
	Log(Error) << "Unable to find interface with ifindex=" << Iface << LogEnd;
	return;
    }

    opts.first();
    while ( opt = opts.get() ) {
	switch (opt->getOptType()) {
	case OPTION_IA_NA:
	{
	    ia = (Ptr*) opt;
	    cfgIa = iface->getIA(ia->getIAID());
	    if (cfgIa)
		cfgIa->setState(state);
	    break;
	}
	case OPTION_IA_TA:
	{
	    ia = (Ptr*) opt;
	    iface->firstTA();
	    cfgTa = iface->getTA();
	    if (cfgTa)
		cfgTa->setState(state);
	    break;
	}
	case OPTION_IA_PD:
	{
	    pd = (Ptr*) opt;
	    cfgPd = iface->getPD(pd->getIAID());
	    if (cfgPd)
		cfgPd->setState(state);
	    break;
	}
	default:
	    continue;
	}
    }
}

void TClntMsgRequest::copyAddrsFromAdvertise(SPtr<TClntMsg> adv)
{
    SPtr<TOpt> opt1, opt2, opt3;
    SPtr<TClntOptIA_NA> ia1, ia2;

    Options.first();
    while (opt1 = Options.get()) {
	if (opt1->getOptType()!=OPTION_IA_NA)
	    continue; // ignore all options except IA_NA
	adv->firstOption();
	while (opt2 = adv->getOption()) {
	    if (opt2->getOptType() != OPTION_IA_NA)
		continue;
	    ia1 = (Ptr*) opt1;
	    ia2 = (Ptr*) opt2;
	    if (ia1->getIAID() != ia2->getIAID())
		continue;

	    // found IA in ADVERTISE, now copy all addrs

	    ia1->delAllOptions();
	    ia2->firstOption();
	    while (opt3 = ia2->getOption()) {
		if (opt3->getOptType() == OPTION_IAADDR)
		    ia1->addOption(opt3);
	    }

	}
    }
    

}

TClntMsgRequest::~TClntMsgRequest()
{
    delete [] pkt;
    pkt=NULL;
}
