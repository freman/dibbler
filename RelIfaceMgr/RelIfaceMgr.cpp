/*
 * Dibbler - a portable DHCPv6
 *
 * authors: Tomasz Mrugalski <thomson@klub.com.pl>
 *          Marek Senderski <msend@o2.pl>
 *
 * released under GNU GPL v2 or later licence
 *
 * $Id: RelIfaceMgr.cpp,v 1.2 2005-01-11 23:35:22 thomson Exp $
 *
 * $Log: not supported by cvs2svn $
 * Revision 1.1  2005/01/11 22:53:35  thomson
 * Relay skeleton implemented.
 *
 *
 */

#include "RelCommon.h"
#include "Logger.h"
#include "IPv6Addr.h"
#include "Iface.h"
#include "SocketIPv6.h"
#include "RelIfaceMgr.h"
#include "RelMsgGeneric.h"

/*
 * constructor. Do nothing particular, just invoke IfaceMgr constructor
 */
TRelIfaceMgr::TRelIfaceMgr(string xmlFile) 
    : TIfaceMgr(xmlFile, true) {
}

TRelIfaceMgr::~TRelIfaceMgr() {
    Log(Debug) << "RelIfaceMgr cleanup." << LogEnd;
}

void TRelIfaceMgr::dump()
{
    std::ofstream xmlDump;
    xmlDump.open( this->XmlFile.c_str() );
    xmlDump << *this;
    xmlDump.close();
}


/**
 * sends data to client. Uses multicast address as source
 * @param iface - interface ID
 * @param msg - buffer containing message ready to send
 * @param size - size of message
 * @param addr - destination address
 * returns true if message was send successfully
 */
bool TRelIfaceMgr::send(int ifindex, char *data, int dataLen, 
			SmartPtr<TIPv6Addr> addr, int port) {
    // find this interface
    SmartPtr<TIfaceIface> iface = this->getIfaceByID(ifindex);
    if (!iface) {
	Log(Error)  << "Send failed: No such interface id=" << ifindex << LogEnd;
	return false;
    }

    // find this socket
    SmartPtr<TIfaceSocket> ptrSocket;
    iface->firstSocket();
    ptrSocket = iface->getSocket();
    if (!ptrSocket) {
	Log(Error) << "Send failed: interface " << iface->getName() 
		   << "/" << iface->getID() << " has no open sockets." << LogEnd;
	return false;
    }

    // send it!
    return ptrSocket->send(data, dataLen, addr, port);
}

/**
 * reads messages from all interfaces
 * it's wrapper around IfaceMgr::select(...) method
 * @param timeout - how long can we wait for packets?
 * returns SmartPtr to message object
 */
SmartPtr<TRelMsg> TRelIfaceMgr::select(unsigned long timeout) {
    
    // static buffer speeds things up
    static char data[2048];
    int dataLen=2048;

    SmartPtr<TIPv6Addr> peer (new TIPv6Addr());
    int sockid;
    int msgtype;

    // read data
    sockid = TIfaceMgr::select(timeout, data, dataLen, peer);
    if (sockid>0) {
	if (dataLen<4) {
	    Log(Warning) << "Received message is truncated (" << dataLen << " bytes)." << LogEnd;
	    return 0; //NULL
	}
	
	// check message type
	msgtype = data[0];
	SmartPtr<TMsg> ptr;
	SmartPtr<TIfaceIface> iface;
	SmartPtr<TIfaceSocket> sock;

	// get interface
	iface = this->getIfaceBySocket(sockid);

	sock = iface->getSocketByFD(sockid);

	Log(Debug) << "Received " << dataLen << " bytes on interface " << iface->getName() << "/" 
		   << iface->getID() << " (socket=" << sockid << ", addr=" << *peer << ", port=" 
		   << sock->getFD() << ")." << LogEnd;
	
	if (sock->getPort()!=DHCPSERVER_PORT) {
	    Log(Error) << "Message was received on invalid (" << sock->getPort() << ") port." << LogEnd;
	    return 0;
	}

	// create specific message object
	switch (msgtype) {
	case SOLICIT_MSG:
	case REQUEST_MSG:
	case CONFIRM_MSG:
	case RENEW_MSG:
	case REBIND_MSG:
	case RELEASE_MSG:
	case DECLINE_MSG:
	case INFORMATION_REQUEST_MSG:
	case ADVERTISE_MSG:
	case REPLY_MSG:
	case RECONFIGURE_MSG:
	case RELAY_FORW_MSG: 
	    return this->decodeMsg(iface, peer, data, dataLen);
	case RELAY_REPL_MSG:
	    return this->decodeRelayRepl(iface, peer, data, dataLen);
	}
    } 
    return 0;
}

SmartPtr<TRelMsg> TRelIfaceMgr::decodeRelayRepl(SmartPtr<TIfaceIface> iface, 
						SmartPtr<TIPv6Addr> peer, 
						char * buf, int bufsize) {
    return 0;
}

SmartPtr<TRelMsg> TRelIfaceMgr::decodeMsg(SmartPtr<TIfaceIface> iface, 
					  SmartPtr<TIPv6Addr> peer, 
					  char * buf, int bufsize) {
    int ifindex = iface->getID();
    return new TRelMsgGeneric(this->Ctx, ifindex, peer, buf, bufsize);
    return 0;
}

ostream & operator <<(ostream & strum, TRelIfaceMgr &x) {
    strum << "<RelIfaceMgr>" << std::endl;
    SmartPtr<TIfaceIface> ptr;
    x.IfaceLst.first();
    while ( ptr= (Ptr*) x.IfaceLst.get() ) {
	strum << *ptr;
    }
    strum << "</RelIfaceMgr>" << std::endl;
    return strum;
}