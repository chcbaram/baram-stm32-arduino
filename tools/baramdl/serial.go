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

// USB identity of the bootloader. 0x1209 is pid.codes, the vendor ID shared by
// open source hardware projects. 0xB751 is the same device with the UF2 mass
// storage volume also present, which happens after a reset double-tap; 0xB752
// is the application, which this tool cannot talk to.
const (
	bootVID    = 0x1209
	bootPIDCDC = 0xB750
	bootPIDMSC = 0xB751
	appPID     = 0xB752
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

// listPorts returns the ports whose USB IDs belong to this board, split by
// whether they are the bootloader or a running sketch.
func listPorts() (boot, app []string, err error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return nil, nil, err
	}
	for _, p := range ports {
		if !p.IsUSB || parseHex(p.VID) != bootVID {
			continue
		}
		switch parseHex(p.PID) {
		case bootPIDCDC, bootPIDMSC:
			boot = append(boot, p.Name)
		case appPID:
			app = append(app, p.Name)
		}
	}
	return boot, app, nil
}

// isBootloaderPort reports whether a port name belongs to the bootloader.
// Unknown ports answer true, so an explicitly named port the enumerator cannot
// classify is still tried.
func isBootloaderPort(name string) bool {
	boot, app, err := listPorts()
	if err != nil {
		return true
	}
	for _, p := range boot {
		if p == name {
			return true
		}
	}
	for _, p := range app {
		if p == name {
			return false
		}
	}
	return true
}

// findPort locates the bootloader by USB VID/PID rather than by port name,
// which differs per OS and between plug-ins.
//
// If only a running sketch is found, it is asked to hand over with a 1200 bps
// touch and the bootloader's port is waited for. arduino-cli does this itself
// when upload.use_1200bps_touch is set, but doing it here too means the tool
// works the same way when it is run by hand.
func findPort() (string, error) {
	boot, app, err := listPorts()
	if err != nil {
		return "", err
	}
	if len(boot) == 0 && len(app) > 0 {
		fmt.Printf("found a running sketch on %s, asking it to enter the bootloader\n", app[0])
		if err := touch1200(app[0]); err != nil {
			return "", err
		}
		if boot, err = waitForBootloader(10 * time.Second); err != nil {
			return "", err
		}
	}

	switch len(boot) {
	case 0:
		return "", fmt.Errorf("no board found (looking for USB %04X:%04X, %04X:%04X or %04X:%04X).\n"+
			"Press reset twice quickly to stay in the bootloader, or pass --port",
			bootVID, bootPIDCDC, bootVID, bootPIDMSC, bootVID, appPID)
	case 1:
		return boot[0], nil
	default:
		return "", fmt.Errorf("found %d boards (%s); pass --port to choose one",
			len(boot), strings.Join(boot, ", "))
	}
}

// touch1200 opens the port at 1200 baud and closes it, dropping DTR. The sketch
// recognises that pair and reboots into the bootloader.
func touch1200(name string) error {
	port, err := serial.Open(name, &serial.Mode{BaudRate: 1200})
	if err != nil {
		return fmt.Errorf("1200 bps touch on %s: %w", name, err)
	}
	_ = port.SetDTR(false)
	time.Sleep(50 * time.Millisecond)
	return port.Close()
}

// waitForBootloader polls for the bootloader's port. The board disappears from
// the bus and comes back as a different device, so there is nothing to hold on
// to across the reset.
func waitForBootloader(timeout time.Duration) ([]string, error) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		time.Sleep(250 * time.Millisecond)
		boot, _, err := listPorts()
		if err != nil {
			return nil, err
		}
		if len(boot) > 0 {
			// Give the host a moment to finish setting the port up.
			time.Sleep(250 * time.Millisecond)
			return boot, nil
		}
	}
	return nil, fmt.Errorf("the board did not come back as the bootloader within %s", timeout)
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
