package main

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"time"
)

// Packet framing, matching cmd.c in the bootloader:
//
//	STX0(0x02) STX1(0xFD) type cmd_l cmd_h err_l err_h len_l len_h [data...] checksum
//	checksum = (~sum(header+data)) + 1
const (
	stx0 = 0x02
	stx1 = 0xFD

	pktTypeCmd  = 0x00
	pktTypeResp = 0x01

	headerLen = 9

	// HW_CMD_MAX_DATA_LENGTH in the bootloader's hw_def.h. Four of those bytes
	// carry the destination offset, so a write chunk can be four smaller.
	maxDataLen  = 1024
	maxWriteLen = maxDataLen - 4
)

// Bootloader commands (cmd_boot.c).
const (
	cmdInfo     = 0x0000
	cmdVersion  = 0x0001
	cmdFwBegin  = 0x0002
	cmdFwErase  = 0x0003
	cmdFwWrite  = 0x0004
	cmdFwRead   = 0x0005
	cmdFwEnd    = 0x0006
	cmdFwVerify = 0x0007
	cmdFwUpdate = 0x0008
	cmdFwJump   = 0x0009
)

// Image classification returned by FW_VERIFY (BootImgType_t in boot.h).
const (
	imgNone = 0 // no valid image
	imgRaw  = 1 // the vector table looks plausible, nothing more
	imgVer  = 2 // firm_ver_t declared a size
	imgTag  = 3 // CRC verified
)

func imgTypeName(t uint8) string {
	switch t {
	case imgNone:
		return "NONE"
	case imgRaw:
		return "RAW"
	case imgVer:
		return "VER"
	case imgTag:
		return "TAG"
	}
	return fmt.Sprintf("?%d", t)
}

// transport is the byte channel the protocol runs over. The bootloader
// abstracts the same way (cmd_driver_t), so CDC, HID and TCP all speak this.
type transport interface {
	Write(p []byte) error
	Read(p []byte) (int, error)
	FlushInput() error
	Close() error
}

type response struct {
	cmd  uint16
	err  uint16
	data []byte
}

func buildPacket(cmd uint16, data []byte) []byte {
	buf := make([]byte, 0, headerLen+len(data)+1)
	buf = append(buf, stx0, stx1, pktTypeCmd)
	buf = binary.LittleEndian.AppendUint16(buf, cmd)
	buf = binary.LittleEndian.AppendUint16(buf, 0) // err, unused in a request
	buf = binary.LittleEndian.AppendUint16(buf, uint16(len(data)))
	buf = append(buf, data...)

	var sum byte
	for _, b := range buf {
		sum += b
	}
	return append(buf, ^sum+1)
}

type client struct {
	t   transport
	buf []byte
	rx  []byte
}

func newClient(t transport) *client {
	return &client{t: t, rx: make([]byte, 4096)}
}

// request sends one command and waits for the matching response.
func (c *client) request(cmd uint16, data []byte, timeout time.Duration) (*response, error) {
	if err := c.t.FlushInput(); err != nil {
		return nil, err
	}
	if err := c.t.Write(buildPacket(cmd, data)); err != nil {
		return nil, fmt.Errorf("write: %w", err)
	}

	c.buf = c.buf[:0]
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		n, err := c.t.Read(c.rx)
		if err != nil {
			return nil, fmt.Errorf("read: %w", err)
		}
		if n > 0 {
			c.buf = append(c.buf, c.rx[:n]...)
		}

		for {
			i := bytes.Index(c.buf, []byte{stx0, stx1})
			if i < 0 {
				// Keep the trailing byte: it may be a split STX pair.
				if len(c.buf) > 1 {
					c.buf = c.buf[len(c.buf)-1:]
				}
				break
			}
			if len(c.buf)-i < headerLen {
				c.buf = c.buf[i:]
				break
			}
			h := c.buf[i:]
			typ := h[2]
			rcmd := binary.LittleEndian.Uint16(h[3:])
			rerr := binary.LittleEndian.Uint16(h[5:])
			rlen := binary.LittleEndian.Uint16(h[7:])

			// Anything that is not our response is skipped rather than
			// trusted. Opening the port at the CLI baud rate makes the
			// bootloader echo what we send, and mistaking an echo for a
			// response reports a silent success.
			if typ != pktTypeResp || rcmd != cmd {
				c.buf = c.buf[i+2:]
				continue
			}
			if int(rlen) > maxDataLen {
				c.buf = c.buf[i+2:]
				continue
			}
			if len(c.buf)-i < headerLen+int(rlen)+1 {
				c.buf = c.buf[i:]
				break
			}
			payload := append([]byte(nil), h[headerLen:headerLen+int(rlen)]...)
			c.buf = c.buf[i+headerLen+int(rlen)+1:]
			return &response{cmd: rcmd, err: rerr, data: payload}, nil
		}
	}
	return nil, fmt.Errorf("command 0x%04X timed out after %s (%d bytes buffered)", cmd, timeout, len(c.buf))
}

// call is request plus the error-code check almost every caller wants.
func (c *client) call(cmd uint16, data []byte, timeout time.Duration) ([]byte, error) {
	r, err := c.request(cmd, data, timeout)
	if err != nil {
		return nil, err
	}
	if r.err != 0 {
		return nil, fmt.Errorf("command 0x%04X returned error 0x%04X", cmd, r.err)
	}
	return r.data, nil
}
