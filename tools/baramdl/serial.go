package main

import (
	"fmt"
	"strings"
	"time"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

// The bootloader decides between its CLI and the packet protocol by looking at
// the baud rate the host opened the port with (cdcGetType()). At 115200 the CLI
// owns the CDC endpoint and command packets are echoed rather than answered.
const cmdBaudRate = 921600

// USB identity of the bootloader. 0xB011 is the same device with the UF2 mass
// storage volume also present, which happens after a reset double-tap.
const (
	bootVID     = 0xCAFE
	bootPIDCDC  = 0xB010
	bootPIDMSC  = 0xB011
)

type serialTransport struct {
	port serial.Port
}

func openSerial(name string) (*serialTransport, error) {
	port, err := serial.Open(name, &serial.Mode{BaudRate: cmdBaudRate})
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", name, err)
	}
	// Reads must be able to return early; the protocol layer owns the real
	// timeout and needs to see partial data as it arrives.
	if err := port.SetReadTimeout(100 * time.Millisecond); err != nil {
		port.Close()
		return nil, err
	}
	return &serialTransport{port: port}, nil
}

func (s *serialTransport) Write(p []byte) error {
	for len(p) > 0 {
		n, err := s.port.Write(p)
		if err != nil {
			return err
		}
		p = p[n:]
	}
	return s.port.Drain()
}

func (s *serialTransport) Read(p []byte) (int, error) {
	n, err := s.port.Read(p)
	if err != nil {
		return 0, err
	}
	return n, nil
}

func (s *serialTransport) FlushInput() error { return s.port.ResetInputBuffer() }
func (s *serialTransport) Close() error      { return s.port.Close() }

// findPort locates the bootloader by USB VID/PID rather than by port name,
// which differs per OS and between plug-ins.
func findPort() (string, error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return "", err
	}
	var matches []string
	for _, p := range ports {
		if !p.IsUSB {
			continue
		}
		vid, pid := parseHex(p.VID), parseHex(p.PID)
		if vid == bootVID && (pid == bootPIDCDC || pid == bootPIDMSC) {
			matches = append(matches, p.Name)
		}
	}
	switch len(matches) {
	case 0:
		return "", fmt.Errorf("no board found (looking for USB %04X:%04X or %04X:%04X).\n"+
			"Put the board in bootloader mode by pressing reset twice quickly, or pass --port",
			bootVID, bootPIDCDC, bootVID, bootPIDMSC)
	case 1:
		return matches[0], nil
	default:
		return "", fmt.Errorf("found %d boards (%s); pass --port to choose one",
			len(matches), strings.Join(matches, ", "))
	}
}

func parseHex(s string) uint64 {
	var v uint64
	for _, c := range strings.ToLower(s) {
		var d uint64
		switch {
		case c >= '0' && c <= '9':
			d = uint64(c - '0')
		case c >= 'a' && c <= 'f':
			d = uint64(c-'a') + 10
		default:
			return 0
		}
		v = v<<4 | d
	}
	return v
}
