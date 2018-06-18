// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "dwc2.h"

#define DWC_REG_DATA_FIFO_START 0x1000
#define DWC_REG_DATA_FIFO(regs, ep)	((volatile uint32_t*)((uint8_t*)regs + (ep + 1) * 0x1000))

#define CLEAR_IN_EP_INTR(__epnum, __intr) \
do { \
        dwc_diepint_t diepint = {0}; \
	diepint.__intr = 1; \
	regs->depin[__epnum].diepint = diepint; \
} while (0)

#define CLEAR_OUT_EP_INTR(__epnum, __intr) \
do { \
        dwc_doepint_t doepint = {0}; \
	doepint.__intr = 1; \
	regs->depout[__epnum].doepint = doepint; \
} while (0)


static void dwc_set_address(dwc_usb_t* dwc, uint8_t address) {
    dwc_regs_t* regs = dwc->regs;
printf("dwc_set_address %u\n", address);
    regs->dcfg.devaddr = address;
}

static void dwc2_ep0_out_start(dwc_usb_t* dwc)  {
    printf("dwc2_ep0_out_start\n");

    dwc_regs_t* regs = dwc->regs;

	dwc_deptsiz0_t doeptsize0 = {0};
	dwc_depctl_t doepctl = {0};

	doeptsize0.supcnt = 3;
	doeptsize0.pktcnt = 1;
	doeptsize0.xfersize = 8 * 3;
    regs->depout[0].doeptsiz.val = doeptsize0.val;

//??    dwc->ep0_state = EP0_STATE_IDLE;

	doepctl.epena = 1;
    regs->depout[0].doepctl = doepctl;
}

static void dwc_ep_start_transfer(dwc_usb_t* dwc, unsigned ep_num, bool is_in) {
printf("dwc_ep_start_transfer epnum %u is_in %d\n", ep_num, is_in);
    dwc_regs_t* regs = dwc->regs;
    dwc_endpoint_t* ep = &dwc->eps[ep_num];

	volatile dwc_depctl_t* depctl_reg;
	volatile dwc_deptsiz_t* deptsiz_reg;
	uint32_t ep_mps = ep->max_packet_size;
//    _ep->total_len = _ep->xfer_len;

	if (is_in) {
		depctl_reg = &regs->depin[ep_num].diepctl;
		deptsiz_reg = &regs->depin[ep_num].dieptsiz;
	} else {
		depctl_reg = &regs->depout[ep_num].doepctl;
		deptsiz_reg = &regs->depout[ep_num].doeptsiz;
	}

    dwc_depctl_t depctl = *depctl_reg;
	dwc_deptsiz_t deptsiz = *deptsiz_reg;

	/* Zero Length Packet? */
	if (ep->txn_length == 0) {
		deptsiz.xfersize = is_in ? 0 : ep_mps;
		deptsiz.pktcnt = 1;
	} else {
		deptsiz.pktcnt = (ep->txn_length + (ep_mps - 1)) / ep_mps;
		if (is_in && ep->txn_length < ep_mps) {
			deptsiz.xfersize = ep->txn_length;
		}
		else {
			deptsiz.xfersize = ep->txn_length - ep->txn_offset;
		}
	}

    *deptsiz_reg = deptsiz;

	/* IN endpoint */
	if (is_in) {
		/* First clear it from GINTSTS */
//?????		regs->gintsts.nptxfempty = 0;
		regs->gintmsk.nptxfempty = 1;
	}

	/* EP enable */
	depctl.cnak = 1;
	depctl.epena = 1;

    *depctl_reg = depctl;
}

static void do_setup_status_phase(dwc_usb_t* dwc, bool is_in) {
printf("do_setup_status_phase is_in: %d\n", is_in);
     dwc_endpoint_t* ep = &dwc->eps[0];

	dwc->ep0_state = EP0_STATE_STATUS;

    ep->txn_offset = 0;
    ep->txn_length = 0;

	dwc_ep_start_transfer(dwc, 0, is_in);

	/* Prepare for more SETUP Packets */
	dwc2_ep0_out_start(dwc);
}

static void dwc_ep0_complete_request(dwc_usb_t* dwc) {
     dwc_endpoint_t* ep = &dwc->eps[0];

    if (dwc->ep0_state == EP0_STATE_STATUS) {
printf("dwc_ep0_complete_request EP0_STATE_STATUS\n");
      ep->txn_offset = 0;
       ep->txn_length = 0;
// this interferes with zero length OUT
//    } else if ( ep->txn_length == 0) {
//printf("dwc_ep0_complete_request ep->txn_length == 0\n");
//		dwc_otg_ep_start_transfer(ep);
    } else if (dwc->ep0_state == EP0_STATE_DATA_IN) {
printf("dwc_ep0_complete_request EP0_STATE_DATA_IN\n");
 	   if (ep->txn_offset >= ep->txn_length) {
	        do_setup_status_phase(dwc, false);
       }
    } else {
printf("dwc_ep0_complete_request ep0-OUT\n");
	    do_setup_status_phase(dwc, true);
    }

#if 0
	deptsiz0_data_t deptsiz;
	dwc_ep_t* ep = &pcd->dwc_eps[0].dwc_ep;
	int ret = 0;

	if (EP0_STATUS == pcd->ep0state) {
		ep->start_xfer_buff = 0;
		ep->xfer_buff = 0;
		ep->xfer_len = 0;
		ep->num = 0;
		ret = 1;
	} else if (0 == ep->xfer_len) {
		ep->xfer_len = 0;
		ep->xfer_count = 0;
		ep->sent_zlp = 1;
		ep->num = 0;
		dwc_otg_ep_start_transfer(ep);
		ret = 1;
	} else if (ep->is_in) {
		deptsiz.d32 = dwc_read_reg32(DWC_REG_IN_EP_TSIZE(0));
		if (0 == deptsiz.b.xfersize) {
			/* Is a Zero Len Packet needed? */
			do_setup_status_phase(pcd, 0);
		}
	} else {
		/* ep0-OUT */
		do_setup_status_phase(pcd, 1);
	}

#endif
}

static zx_status_t dwc_handle_setup(dwc_usb_t* dwc, usb_setup_t* setup, void* buffer, size_t length,
                                     size_t* out_actual) {
printf("dwc_handle_setup\n");
    zx_status_t status;
    dwc_endpoint_t* ep = &dwc->eps[0];

    if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) {
        // handle some special setup requests in this driver
        switch (setup->bRequest) {
        case USB_REQ_SET_ADDRESS:
            zxlogf(INFO, "SET_ADDRESS %d\n", setup->wValue);
            dwc_set_address(dwc, setup->wValue);
            *out_actual = 0;
            return ZX_OK;
        case USB_REQ_SET_CONFIGURATION:
            zxlogf(INFO, "SET_CONFIGURATION %d\n", setup->wValue);
//            dwc3_reset_configuration(dwc);
            dwc->configured = false;
            status = usb_dci_control(&dwc->dci_intf, setup, buffer, length, out_actual);
            if (status == ZX_OK && setup->wValue) {
                dwc->configured = true;
//                dwc3_start_eps(dwc);
            }
            return status;
        default:
            // fall through to usb_dci_control()
            break;
        }
    } else if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) &&
               setup->bRequest == USB_REQ_SET_INTERFACE) {
        zxlogf(TRACE, "SET_INTERFACE %d\n", setup->wValue);
//        dwc3_reset_configuration(dwc);
        dwc->configured = false;
        status = usb_dci_control(&dwc->dci_intf, setup, buffer, length, out_actual);
        if (status == ZX_OK) {
            dwc->configured = true;
//            dwc3_start_eps(dwc);
        }
        return status;
    }

    status = usb_dci_control(&dwc->dci_intf, setup, buffer, length, out_actual);
    if (status == ZX_OK) {
        ep->txn_offset = 0;
        ep->txn_length = *out_actual;
    }
    return status;
}

static void pcd_setup(dwc_usb_t* dwc) {
/*
	struct usb_ctrlrequest	ctrl = _pcd->setup_pkt.req;
	struct usb_request *req = &gadget_wrapper.req;
	struct usb_req_flag *req_flag = &gadget_wrapper.req_flag;
	struct usb_gadget_driver *driver = gadget_wrapper.driver;
	struct usb_gadget *gadget = &gadget_wrapper.gadget;
*/
//	dwc_endpoint_t* ep0 = &dwc->eps[0];
    usb_setup_t* setup = &dwc->cur_setup;

	if (!dwc->got_setup) {
printf("no setup\n");
		return;
	}
	dwc->got_setup = false;
//	_pcd->status = 0;


	if (setup->bmRequestType & USB_DIR_IN) {
printf("pcd_setup set EP0_STATE_DATA_IN\n");
		dwc->ep0_state = EP0_STATE_DATA_IN;
	} else {
printf("pcd_setup set EP0_STATE_DATA_OUT\n");
		dwc->ep0_state = EP0_STATE_DATA_OUT;
	}

    if (setup->wLength > 0 && dwc->ep0_state == EP0_STATE_DATA_OUT) {
printf("queue read\n");
        // queue a read for the data phase
        dwc->ep0_state = EP0_STATE_DATA_OUT;
        dwc_ep_start_transfer(dwc, 0, false);
    } else {
        size_t actual;
        zx_status_t status = dwc_handle_setup(dwc, setup, io_buffer_virt(&dwc->ep0_buffer),
                                              dwc->ep0_buffer.size, &actual);
        zxlogf(INFO, "dwc_handle_setup returned %d actual %zu\n", status, actual);
//            if (status != ZX_OK) {
//                dwc3_cmd_ep_set_stall(dwc, EP0_OUT);
//                dwc3_queue_setup_locked(dwc);
//                break;
//            }

        if (dwc->ep0_state == EP0_STATE_DATA_IN && setup->wLength > 0) {
            printf("queue a write for the data phase\n");
            io_buffer_cache_flush(&dwc->ep0_buffer, 0, actual);
            dwc->ep0_state = EP0_STATE_DATA_IN;
            dwc_ep_start_transfer(dwc, 0, true);
        } else {
			dwc_ep0_complete_request(dwc);
        }
    }


#if 0
	if ((ctrl.bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD) {
		// handle non-standard (class/vendor) requests in the gadget driver
		printf("Vendor requset\n");
		f_dwc_otg_ep_req_start(_pcd, 0, 1, req);
		return;
	}

	/** @todo NGS: Handle bad setup packet? */
	switch (ctrl.bRequest) {
	case USB_REQ_GET_STATUS:
		_pcd->status = 0;
		req->buf = (char *)&_pcd->status;
		req->length = 2;
		f_dwc_otg_ep_req_start(_pcd, 0, 1, req);
		break;
	case USB_REQ_SET_ADDRESS:
		if (USB_RECIP_DEVICE == ctrl.bRequestType) {
			dcfg_data_t dcfg = {0};
			dcfg.b.devaddr = ctrl.wValue;
			dwc_modify_reg32(DWC_REG_DCFG, 0, dcfg.d32);
			do_setup_status_phase(_pcd, 1);
		}
		break;
	case USB_REQ_SET_INTERFACE:
	case USB_REQ_SET_CONFIGURATION:
		/* Configuration changed */
		req_flag->request_config = 1;
	default:
		DBG("Call the Gadget Driver's setup functions\n");
		/* Call the Gadget Driver's setup functions */
		driver->setup(gadget, &ctrl);
		break;
	}
#endif
}


static void dwc_handle_ep0(dwc_usb_t* dwc) {
    printf("dwc_handle_ep0\n");

/*    
  	pcd_struct_t * _pcd = &gadget_wrapper.pcd;
	dwc_ep_t * ep0 = &_pcd->dwc_eps[0].dwc_ep;
	struct usb_req_flag *req_flag = &gadget_wrapper.req_flag;
*/
	switch (dwc->ep0_state) {
	case EP0_STATE_IDLE: {
printf("dwc_handle_ep0 EP0_STATE_IDLE\n");
//		req_flag->request_config = 0;
		pcd_setup(dwc);
        break;
    }
	case EP0_STATE_DATA_IN:
    printf("dwc_handle_ep0 EP0_STATE_DATA_IN\n");
//		if (ep0->xfer_count < ep0->total_len)
//			printf("FIX ME!! dwc_otg_ep0_continue_transfer!\n");
//		else
			dwc_ep0_complete_request(dwc);
		break;
	case EP0_STATE_DATA_OUT:
    printf("dwc_handle_ep0 EP0_STATE_DATA_OUT\n");
		dwc_ep0_complete_request(dwc);
		break;
	case EP0_STATE_STATUS:
    printf("dwc_handle_ep0 EP0_STATE_STATUS\n");
		dwc_ep0_complete_request(dwc);
		/* OUT for next SETUP */
		dwc->ep0_state = EP0_STATE_IDLE;
//		ep0->stopped = 1;
//		ep0->is_in = 0;
		break;

	case EP0_STATE_STALL:
	default:
		printf("EP0 state is %d, should not get here pcd_setup()\n", dwc->ep0_state);
		break;
    }
}

static void dwc_complete_ep(dwc_usb_t* dwc, uint32_t ep_num, int is_in) {
    printf("dwc_complete_ep\n");

#if 0
	deptsiz_data_t deptsiz;
	pcd_struct_t *pcd = &gadget_wrapper.pcd;
	dwc_ep_t * ep;
	uint32_t epnum = ep_num;

	if (ep_num) {
		if (!is_in)
			epnum = ep_num + 1;
	}

	ep = &pcd->dwc_eps[epnum].dwc_ep;

	if (is_in) {
		pcd->dwc_eps[epnum].req->actual = ep->xfer_len;
		deptsiz.d32 = dwc_read_reg32(DWC_REG_IN_EP_TSIZE(ep_num));
		if (deptsiz.b.xfersize == 0 && deptsiz.b.pktcnt == 0 &&
                    ep->xfer_count == ep->xfer_len) {
			ep->start_xfer_buff = 0;
			ep->xfer_buff = 0;
			ep->xfer_len = 0;
		}
		pcd->dwc_eps[epnum].req->status = 0;
	} else {
		deptsiz.d32 = dwc_read_reg32(DWC_REG_OUT_EP_TSIZE(ep_num));
		pcd->dwc_eps[epnum].req->actual = ep->xfer_count;
		ep->start_xfer_buff = 0;
		ep->xfer_buff = 0;
		ep->xfer_len = 0;
		pcd->dwc_eps[epnum].req->status = 0;
	}

	if (pcd->dwc_eps[epnum].req->complete) {
		pcd->dwc_eps[epnum].req->complete((struct usb_ep *)(pcd->dwc_eps[epnum].priv), pcd->dwc_eps[epnum].req);
	}
#endif
}


void dwc_flush_fifo(dwc_usb_t* dwc, const int num) {
    dwc_regs_t* regs = dwc->regs;

    dwc_grstctl_t grstctl = {0};

	grstctl.txfflsh = 1;
	grstctl.txfnum = num;
	regs->grstctl = grstctl;
	
    uint32_t count = 0;
	do {
	    grstctl = regs->grstctl;
		if (++count > 10000)
			break;
	} while (grstctl.txfflsh == 1);

    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));

	if (num == 0) {
		return;
    }

    grstctl.val = 0;
	grstctl.rxfflsh = 1;
	regs->grstctl = grstctl;

	count = 0;
	do {
	    grstctl = regs->grstctl;
		if (++count > 10000)
			break;
	} while (grstctl.rxfflsh == 1);

    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
}

void dwc_handle_reset_irq(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;

	printf("\nUSB RESET\n");

    dwc->ep0_state = EP0_STATE_DISCONNECTED;

	/* Clear the Remote Wakeup Signalling */
	regs->dctl.rmtwkupsig = 1;

	for (int i = 0; i < MAX_EPS_CHANNELS; i++) {
	     dwc_depctl_t diepctl = regs->depin[i].diepctl;

        if (diepctl.epena) {
            // disable all active IN EPs
            diepctl.snak = 1;
            diepctl.epdis = 1;
    	    regs->depin[i].diepctl = diepctl;
        }

        regs->depout[i].doepctl.snak = 1;
	}

	/* Flush the NP Tx FIFO */
	dwc_flush_fifo(dwc, 0);

	/* Flush the Learning Queue */
	regs->grstctl.intknqflsh = 1;

    // EPO IN and OUT
	regs->daintmsk = (1 < DWC_EP_IN_SHIFT) | (1 < DWC_EP_OUT_SHIFT);

    dwc_doepint_t doepmsk = {0};
	doepmsk.setup = 1;
	doepmsk.xfercompl = 1;
	doepmsk.ahberr = 1;
	doepmsk.epdisabled = 1;
	regs->doepmsk = doepmsk;

    dwc_diepint_t diepmsk = {0};
	diepmsk.xfercompl = 1;
	diepmsk.timeout = 1;
	diepmsk.epdisabled = 1;
	diepmsk.ahberr = 1;
	regs->diepmsk = diepmsk;

	/* Reset Device Address */
	regs->dcfg.devaddr = 0;

	/* setup EP0 to receive SETUP packets */
	dwc2_ep0_out_start(dwc);
}

void dwc_handle_enumdone_irq(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;

	zxlogf(INFO, "dwc_handle_enumdone_irq\n");


    if (dwc->astro_usb.ops) {
        astro_usb_do_usb_tuning(&dwc->astro_usb, false, false);
    }

    dwc->ep0_state = EP0_STATE_IDLE;

    dwc->eps[0].max_packet_size = 64;

    regs->depin[0].diepctl.mps = DWC_DEP0CTL_MPS_64;
    regs->depout[0].doepctl.epena = 1;

#if 0 // astro future use
	depctl.d32 = dwc_read_reg32(DWC_REG_IN_EP_REG(1));
	if (!depctl.b.usbactep) {
		depctl.b.mps = BULK_EP_MPS;
		depctl.b.eptype = 2;//BULK_STYLE
		depctl.b.setd0pid = 1;
		depctl.b.txfnum = 0;   //Non-Periodic TxFIFO
		depctl.b.usbactep = 1;
		dwc_write_reg32(DWC_REG_IN_EP_REG(1), depctl.d32);
	}

	depctl.d32 = dwc_read_reg32(DWC_REG_OUT_EP_REG(2));
	if (!depctl.b.usbactep) {
		depctl.b.mps = BULK_EP_MPS;
		depctl.b.eptype = 2;//BULK_STYLE
		depctl.b.setd0pid = 1;
		depctl.b.txfnum = 0;   //Non-Periodic TxFIFO
		depctl.b.usbactep = 1;
		dwc_write_reg32(DWC_REG_OUT_EP_REG(2), depctl.d32);
	}
#endif

    regs->dctl.cgnpinnak = 1;

	/* high speed */
#if 1 // astro
	regs->gusbcfg.usbtrdtim = 9;
#else
	regs->gusbcfg.usbtrdtim = 5;
#endif
}

void dwc_handle_rxstsqlvl_irq(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;

//why?	regs->gintmsk.rxstsqlvl = 0;

	/* Get the Status from the top of the FIFO */
	 dwc_grxstsp_t grxstsp = regs->grxstsp;
printf("dwc_handle_rxstsqlvl_irq epnum: %u bcnt: %u pktsts: %u\n", grxstsp.epnum, grxstsp.bcnt, grxstsp.pktsts);
	 
	if (grxstsp.epnum != 0)
		grxstsp.epnum = 2; // ??????
	/* Get pointer to EP structure */
//	ep = &pcd->dwc_eps[status.b.epnum].dwc_ep;

	switch (grxstsp.pktsts) {
	case DWC_STS_DATA_UPDT:
printf("DWC_STS_DATA_UPDT grxstsp.bcnt: %u\n", grxstsp.bcnt);
/*
		if (status.b.bcnt && ep->xfer_buff) {
			dwc_otg_read_packet(ep->xfer_buff, status.b.bcnt);
			ep->xfer_count += status.b.bcnt;
			ep->xfer_buff += status.b.bcnt;
		}
*/
		break;

	case DWC_DSTS_SETUP_UPDT: {
printf("DWC_DSTS_SETUP_UPDT\n"); 
    volatile uint32_t* fifo = (uint32_t *)((uint8_t *)regs + 0x1000);
    uint32_t* dest = (uint32_t*)&dwc->cur_setup;
    dest[0] = *fifo;
    dest[1] = *fifo;
printf("SETUP bmRequestType: 0x%02x bRequest: %u wValue: %u wIndex: %u wLength: %u grxstsp.bcnt %u\n",
       dwc->cur_setup.bmRequestType, dwc->cur_setup.bRequest, dwc->cur_setup.wValue,
       dwc->cur_setup.wIndex, dwc->cur_setup.wLength, grxstsp.bcnt); 
       dwc->got_setup = true;
		break;
	}

	case DWC_DSTS_GOUT_NAK:
printf("DWC_DSTS_GOUT_NAK\n");
break;
	case DWC_STS_XFER_COMP:
printf("DWC_STS_XFER_COMP\n");
break;
	case DWC_DSTS_SETUP_COMP:
printf("DWC_DSTS_SETUP_COMP\n");
break;
	default:
		break;
	}
}


void dwc_handle_inepintr_irq(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;
	uint32_t ep_intr;
	uint32_t epnum = 0;

	/* Read in the device interrupt bits */
	ep_intr = regs->daint;
	ep_intr = (regs->daint & regs->daintmsk);
	ep_intr = (ep_intr & 0xffff);

	/* Clear all the interrupt bits for all IN endpoints in DAINT */
    regs->daint = 0xFFFF;

	/* Service the Device IN interrupts for each endpoint */
	while (ep_intr) {
		if (ep_intr & 0x1) {
		    dwc_diepint_t diepint = regs->depin[epnum].diepint;

			/* Transfer complete */
			if (diepint.xfercompl) {
				/* Disable the NP Tx FIFO Empty Interrrupt  */
		        regs->gintmsk.nptxfempty = 0;
				/* Clear the bit in DIEPINTn for this interrupt */
				regs->depin[epnum].diepint.xfercompl = 1;
				/* Complete the transfer */
				if (0 == epnum) {
					dwc_handle_ep0(dwc);
				} else {
					dwc_complete_ep(dwc, epnum, 1);
					if (diepint.nak) {
						CLEAR_IN_EP_INTR(epnum, nak);
				    }
				}
			}
			/* Endpoint disable  */
			if (diepint.epdisabled) {
				/* Clear the bit in DIEPINTn for this interrupt */
				CLEAR_IN_EP_INTR(epnum, epdisabled);
			}
			/* AHB Error */
			if (diepint.ahberr) {
				/* Clear the bit in DIEPINTn for this interrupt */
				CLEAR_IN_EP_INTR(epnum, ahberr);
			}
			/* TimeOUT Handshake (non-ISOC IN EPs) */
			if (diepint.timeout) {
//				handle_in_ep_timeout_intr(epnum);
printf("TODO handle_in_ep_timeout_intr\n");
				CLEAR_IN_EP_INTR(epnum, timeout);
			}
			/** IN Token received with TxF Empty */
			if (diepint.intktxfemp) {
				CLEAR_IN_EP_INTR(epnum, intktxfemp);
			}
			/** IN Token Received with EP mismatch */
			if (diepint.intknepmis) {
				CLEAR_IN_EP_INTR(epnum, intknepmis);
			}
			/** IN Endpoint NAK Effective */
			if (diepint.inepnakeff) {
				CLEAR_IN_EP_INTR(epnum, inepnakeff);
			}
		}
		epnum++;
		ep_intr >>= 1;
	}
}

static void dwc_ep_write_packet(dwc_usb_t* dwc, int epnum, uint32_t byte_count, uint32_t dword_count) {
    printf("dwc_ep_write_packet ep %d byte_count: %u dword_count %u\n", epnum, byte_count, dword_count);
    dwc_regs_t* regs = dwc->regs;
    dwc_endpoint_t* ep = &dwc->eps[0];  // FIXME

	uint32_t i;
	volatile uint32_t* fifo;
	uint32_t temp_data;
    uint8_t *data_buff = io_buffer_virt(&dwc->ep0_buffer) + ep->txn_offset;

	if (ep->txn_offset >= ep->txn_length) {
		printf("dwc_ep_write_packet: No data for EP%d!!!\n", epnum);
		return;
	}

	fifo = DWC_REG_DATA_FIFO(regs, epnum);
printf("fifo: %p\n", fifo);

	for (i = 0; i < dword_count; i++) {
		temp_data = *((uint32_t*)data_buff);
printf("write %08x\n", temp_data);
		*fifo = temp_data;
		data_buff += 4;
	}

	ep->txn_offset += byte_count;
}

void dwc_handle_outepintr_irq(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;

//printf("dwc_handle_outepintr_irq\n");

	uint32_t epnum = 0;

	/* Read in the device interrupt bits */
	uint32_t ep_intr = regs->daint & DWC_EP_OUT_MASK;
	ep_intr >>= DWC_EP_OUT_SHIFT;

	/* Clear the interrupt */
	regs->daint = DWC_EP_OUT_MASK;

	while (ep_intr) {
		if (ep_intr & 1) {
		    dwc_doepint_t doepint = regs->depout[epnum].doepint;
		    doepint.val &= regs->doepmsk.val;
printf("dwc_handle_outepintr_irq doepint.val %08x\n", doepint.val);

			/* Transfer complete */
			if (doepint.xfercompl) {
printf("dwc_handle_outepintr_irq xfercompl\n");
				/* Clear the bit in DOEPINTn for this interrupt */
				CLEAR_OUT_EP_INTR(epnum, xfercompl);

				if (epnum == 0) {
				    if (doepint.setup) { // astro
    					CLEAR_OUT_EP_INTR(epnum, setup);
    			    }
					dwc_handle_ep0(dwc);
				} else {
					dwc_complete_ep(dwc, epnum, 0);
				}
			}
			/* Endpoint disable  */
			if (doepint.epdisabled) {
printf("dwc_handle_outepintr_irq epdisabled\n");
				/* Clear the bit in DOEPINTn for this interrupt */
				CLEAR_OUT_EP_INTR(epnum, epdisabled);
			}
			/* AHB Error */
			if (doepint.ahberr) {
printf("dwc_handle_outepintr_irq ahberr\n");
				CLEAR_OUT_EP_INTR(epnum, ahberr);
			}
			/* Setup Phase Done (contr0l EPs) */
			if (doepint.setup) {
			    if (1) { // astro
					dwc_handle_ep0(dwc);
				}
				CLEAR_OUT_EP_INTR(epnum, setup);
			}
		}
		epnum++;
		ep_intr >>= 1;
	}
}

void dwc_handle_nptxfempty_irq(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;

printf("dwc_handle_nptxfempty_irq\n");

	dwc_gnptxsts_t txstatus = {0};
	dwc_endpoint_t *ep = NULL;
	dwc_depctl_t depctl;
	uint32_t len = 0;
	uint32_t dwords;
	uint32_t epnum = 0;

    /* Get the epnum from the IN Token Learning Queue. */
	for (epnum = 0; epnum < 1/* MAX_EPS_CHANNELS */; epnum++) {
		ep = &dwc->eps[epnum];

		/* IN endpoint ? */
		if (epnum > 0 && DWC_EP_IS_OUT(epnum)) {
printf("not IN\n");
			continue;
		}

//!!		if (ep->type == DWC_OTG_EP_TYPE_INTR && ep->xfer_len == 0)
//			continue;

		depctl = regs->depin[epnum].diepctl;
		if (depctl.epena != 1) {
printf("not enabled\n");
			continue;
        }

		/* While there is space in the queue and space in the FIFO and
		 * More data to tranfer, Write packets to the Tx FIFO */
		txstatus = regs->gnptxsts;
		while  (/*txstatus.b.nptxqspcavail > 0 &&
			txstatus.b.nptxfspcavail > dwords &&*/
		            ep->txn_offset < ep->txn_length) {
			uint32_t retry = 1000000;

			len = ep->txn_length - ep->txn_offset;
			if (len > ep->max_packet_size)
				len = ep->max_packet_size;

			dwords = (len + 3) >> 2;

			while (retry--) {
				txstatus = regs->gnptxsts;
				if (txstatus.nptxqspcavail > 0 && txstatus.nptxfspcavail > dwords)
					break;
			}
			if (0 == retry) {
				printf("TxFIFO FULL: Can't trans data to HOST !\n");
				break;
			}
			/* Write the FIFO */
			dwc_ep_write_packet(dwc, epnum, len, dwords);
		}
	}
}

void dwc_handle_usbsuspend_irq(dwc_usb_t* dwc) {
    printf("dwc_handle_usbsuspend_irq\n");
}

static void dwc_request_queue(void* ctx, usb_request_t* req) {
/*    dwc_t* dwc = ctx;

    zxlogf(LTRACE, "dwc_request_queue ep: %u\n", req->header.ep_address);
    unsigned ep_num = dwc_ep_num(req->header.ep_address);
    if (ep_num < 2 || ep_num >= countof(dwc->eps)) {
        zxlogf(ERROR, "dwc_request_queue: bad ep address 0x%02X\n", req->header.ep_address);
        usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0);
        return;
    }

    dwc_ep_queue(dwc, ep_num, req);
*/
}

static zx_status_t dwc_set_interface(void* ctx, usb_dci_interface_t* dci_intf) {
    dwc_usb_t* dwc = ctx;
    memcpy(&dwc->dci_intf, dci_intf, sizeof(dwc->dci_intf));
    return ZX_OK;
}

static zx_status_t dwc_config_ep(void* ctx, usb_endpoint_descriptor_t* ep_desc,
                                  usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
//    dwc_usb_t* dwc = ctx;
//    return dwc_ep_config(dwc, ep_desc, ss_comp_desc);
return -1;
}

static zx_status_t dwc_disable_ep(void* ctx, uint8_t ep_addr) {
//    dwc_usb_t* dwc = ctx;
//    return dwc_ep_disable(dwc, ep_addr);
return -1;
}

static zx_status_t dwc_set_stall(void* ctx, uint8_t ep_address) {
//    dwc_usb_t* dwc = ctx;
//    return dwc_ep_set_stall(dwc, dwc_ep_num(ep_address), true);
return -1;
}

static zx_status_t dwc_clear_stall(void* ctx, uint8_t ep_address) {
//    dwc_usb_t* dwc = ctx;
//    return dwc_ep_set_stall(dwc, dwc_ep_num(ep_address), false);
return -1;
}

static zx_status_t dwc_get_bti(void* ctx, zx_handle_t* out_handle) {
    dwc_usb_t* dwc = ctx;
    *out_handle = dwc->bti_handle;
    return ZX_OK;
}

usb_dci_protocol_ops_t dwc_dci_protocol = {
    .request_queue = dwc_request_queue,
    .set_interface = dwc_set_interface,
    .config_ep = dwc_config_ep,
    .disable_ep = dwc_disable_ep,
    .ep_set_stall = dwc_set_stall,
    .ep_clear_stall = dwc_clear_stall,
    .get_bti = dwc_get_bti,
};