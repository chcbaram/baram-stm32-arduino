// baramdl writes a firmware image to a BARAM STM32 board through the
// bootloader's own USB CDC interface.
//
// The bootloader exposes a packet protocol (cmd_boot.c) over CDC, HID and TCP.
// CDC is used here: it is bulk rather than 64-byte reports, which measured
// about 264 KB/s against 40 KB/s for HID on the same board, and it needs no
// HID library, so this builds for every platform with plain `go build`.
//
//	baramdl --write app.bin           find the board, write, verify
//	baramdl --info                    report what is connected
//	baramdl --port /dev/cu.usbmodem1  pick the port explicitly
//
// It also produces and copies UF2 files, for the drag-and-drop path the
// bootloader offers on its mass storage volume:
//
//	baramdl --uf2 app.bin app.uf2     convert
//	baramdl --uf2-copy app.uf2        convert if needed, then copy to the drive
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	// FW_BEGIN erases the tag sector, and FW_ERASE clears the whole
	// application area, both of which take a while on a QSPI part.
	eraseTimeout  = 60 * time.Second
	writeTimeout  = 5 * time.Second
	verifyTimeout = 30 * time.Second
	shortTimeout  = 3 * time.Second
)

type bootInfo struct {
	Magic       uint32
	Mode        uint32
	BootAddr    uint32
	BootSize    uint32
	FirmAddr    uint32
	FirmVecAddr uint32
	FirmSize    uint32
	TagSize     uint32
	MaxFwSize   uint32
	FamilyID    uint32
	Name        string
	Version     string
}

const (
	devModeBoot = 0
	devModeApp  = 1
)

// boot_info_t in cmd_boot.c: ten uint32 then two 32-byte strings, packed.
const bootInfoSize = 10*4 + 32 + 32

func parseInfo(d []byte) (*bootInfo, error) {
	if len(d) < bootInfoSize {
		return nil, fmt.Errorf("INFO response is %d bytes, expected %d", len(d), bootInfoSize)
	}
	u := func(i int) uint32 { return binary.LittleEndian.Uint32(d[i*4:]) }
	return &bootInfo{
		Magic: u(0), Mode: u(1), BootAddr: u(2), BootSize: u(3),
		FirmAddr: u(4), FirmVecAddr: u(5), FirmSize: u(6), TagSize: u(7),
		MaxFwSize: u(8), FamilyID: u(9),
		Name:    cstr(d[40:72]),
		Version: cstr(d[72:104]),
	}, nil
}

type versionInfo struct {
	ImgType uint8
	FwSize  uint32
	FwCRC   uint32
	Name    string
	Version string
}

// The VERSION response: img_type, three reserved bytes, two uint32, two names.
const versionSize = 4 + 4 + 4 + 32 + 32

func parseVersion(d []byte) (*versionInfo, error) {
	if len(d) < versionSize {
		return nil, fmt.Errorf("VERSION response is %d bytes, expected %d", len(d), versionSize)
	}
	return &versionInfo{
		ImgType: d[0],
		FwSize:  binary.LittleEndian.Uint32(d[4:]),
		FwCRC:   binary.LittleEndian.Uint32(d[8:]),
		Name:    cstr(d[12:44]),
		Version: cstr(d[44:76]),
	}, nil
}

func cstr(b []byte) string {
	for i, c := range b {
		if c == 0 {
			return string(b[:i])
		}
	}
	return string(b)
}

func main() {
	var (
		portName = flag.String("port", "", "serial port (default: find by USB VID/PID)")
		write    = flag.String("write", "", "firmware .bin to write")
		info     = flag.Bool("info", false, "report the connected board and exit")
		jump     = flag.Bool("jump", false, "leave the bootloader and run the application")
		uf2Out   = flag.String("uf2", "", "convert a .bin to UF2; pass the output path as the next argument")
		uf2Copy  = flag.String("uf2-copy", "", "copy a .bin or .uf2 onto the bootloader's mass storage volume")
		verbose  = flag.Bool("v", false, "verbose")
	)
	flag.Parse()

	// UF2 work needs no serial port, so handle it before looking for a board.
	if *uf2Out != "" {
		out := flag.Arg(0)
		if out == "" {
			out = strings.TrimSuffix(*uf2Out, filepath.Ext(*uf2Out)) + ".uf2"
		}
		if err := writeUF2(*uf2Out, out); err != nil {
			fmt.Fprintln(os.Stderr, "error:", err)
			os.Exit(1)
		}
		return
	}
	if *uf2Copy != "" {
		if err := copyUF2(*uf2Copy); err != nil {
			fmt.Fprintln(os.Stderr, "error:", err)
			os.Exit(1)
		}
		return
	}

	if err := run(*portName, *write, *info, *jump, *verbose); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(portName, writePath string, showInfo, doJump, verbose bool) error {
	if writePath == "" && !showInfo && !doJump {
		flag.Usage()
		return fmt.Errorf("nothing to do: pass --write, --info or --jump")
	}

	var image []byte
	if writePath != "" {
		var err error
		image, err = os.ReadFile(writePath)
		if err != nil {
			return err
		}
		if len(image) == 0 {
			return fmt.Errorf("%s is empty", writePath)
		}
	}

	// A port given with --port is only a hint. arduino-cli passes the port the
	// sketch was on, and by the time the upload tool runs the board has already
	// rebooted into the bootloader and come back under a different name. So try
	// it, and fall back to finding the bootloader ourselves.
	var (
		t   *serialTransport
		c   *client
		raw []byte
	)
	tryPort := func(name string) error {
		var err error
		if t, err = openSerial(name); err != nil {
			return err
		}
		c = newClient(t)
		if raw, err = c.call(cmdInfo, nil, shortTimeout); err != nil {
			t.Close()
			t = nil
			return err
		}
		if verbose {
			fmt.Printf("port     %s @ %d\n", name, cmdBaudRate)
		}
		return nil
	}

	if portName != "" {
		if err := tryPort(portName); err != nil && verbose {
			fmt.Printf("%s did not answer, looking for the board\n", portName)
		}
	}
	if t == nil {
		found, err := findPort()
		if err != nil {
			return err
		}
		if err := tryPort(found); err != nil {
			return fmt.Errorf("the board did not answer on %s: %w\n"+
				"Is it in bootloader mode? Press reset twice quickly to stay there", found, err)
		}
	}
	defer t.Close()
	inf, err := parseInfo(raw)
	if err != nil {
		return err
	}

	if showInfo || verbose {
		printInfo(inf)
		if v, err := c.call(cmdVersion, nil, shortTimeout); err == nil {
			if ver, err := parseVersion(v); err == nil {
				printVersion(ver)
			}
		}
	}
	if showInfo && writePath == "" && !doJump {
		return nil
	}

	if inf.Mode != devModeBoot {
		return fmt.Errorf("the board is running the application, not the bootloader.\n" +
			"Press reset twice quickly to stay in the bootloader, then try again")
	}

	if writePath != "" {
		if err := writeImage(c, image, filepath.Base(writePath), inf); err != nil {
			return err
		}
	}
	if doJump {
		if _, err := c.call(cmdFwJump, nil, shortTimeout); err != nil {
			return err
		}
		fmt.Println("jumped to the application")
	}
	return nil
}

func writeImage(c *client, image []byte, name string, inf *bootInfo) error {
	if uint32(len(image)) > inf.MaxFwSize {
		return fmt.Errorf("%s is %d bytes, more than the %d the board has room for",
			name, len(image), inf.MaxFwSize)
	}

	fmt.Printf("writing %s, %d bytes to 0x%08X\n", name, len(image), inf.FirmVecAddr)

	size := make([]byte, 4)
	binary.LittleEndian.PutUint32(size, uint32(len(image)))

	// FW_BEGIN erases the tag first, so an interrupted transfer leaves an
	// invalid tag rather than a half-written image the bootloader would trust.
	if _, err := c.call(cmdFwBegin, size, eraseTimeout); err != nil {
		return fmt.Errorf("FW_BEGIN: %w", err)
	}
	if _, err := c.call(cmdFwErase, nil, eraseTimeout); err != nil {
		return fmt.Errorf("FW_ERASE: %w", err)
	}

	start := time.Now()
	chunk := make([]byte, 4+maxWriteLen)
	lastPct := -1
	for off := 0; off < len(image); off += maxWriteLen {
		n := maxWriteLen
		if off+n > len(image) {
			n = len(image) - off
		}
		binary.LittleEndian.PutUint32(chunk[:4], uint32(off))
		copy(chunk[4:], image[off:off+n])
		if _, err := c.call(cmdFwWrite, chunk[:4+n], writeTimeout); err != nil {
			return fmt.Errorf("FW_WRITE at offset %d: %w", off, err)
		}
		if pct := (off + n) * 100 / len(image); pct != lastPct {
			fmt.Printf("\r  %3d%%", pct)
			lastPct = pct
		}
	}
	elapsed := time.Since(start)
	fmt.Printf("\r  done, %.1f KB/s\n", float64(len(image))/1024/elapsed.Seconds())

	// FW_END computes the CRC over what was written and lays down the tag.
	if _, err := c.call(cmdFwEnd, nil, verifyTimeout); err != nil {
		return fmt.Errorf("FW_END: %w", err)
	}

	raw, err := c.call(cmdFwVerify, nil, verifyTimeout)
	if err != nil {
		return fmt.Errorf("FW_VERIFY: %w", err)
	}
	if len(raw) < 1 {
		return fmt.Errorf("FW_VERIFY returned no image type")
	}
	img := raw[0]
	fmt.Printf("verified: %s\n", imgTypeName(img))
	if img != imgTag {
		return fmt.Errorf("the image did not verify (got %s, wanted TAG)", imgTypeName(img))
	}
	return nil
}

func printInfo(i *bootInfo) {
	mode := "APP"
	if i.Mode == devModeBoot {
		mode = "BOOT"
	}
	fmt.Printf("board    %s %s (%s mode)\n", i.Name, i.Version, mode)
	fmt.Printf("boot     0x%08X, %d bytes\n", i.BootAddr, i.BootSize)
	fmt.Printf("app      0x%08X vectors, tag at 0x%08X (%d bytes), max %d bytes\n",
		i.FirmVecAddr, i.FirmAddr, i.TagSize, i.MaxFwSize)
	fmt.Printf("family   0x%08X\n", i.FamilyID)
}

func printVersion(v *versionInfo) {
	fmt.Printf("image    %s", imgTypeName(v.ImgType))
	if v.ImgType != imgNone {
		fmt.Printf(", %d bytes, crc 0x%04X, %q %q", v.FwSize, v.FwCRC, v.Name, v.Version)
	}
	fmt.Println()
}

// copyUF2 drops a firmware image onto the bootloader's mass storage volume, the
// same thing dragging the file onto the drive in a file manager does. A .bin is
// converted on the way; a .uf2 is copied as is.
func copyUF2(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if !strings.EqualFold(filepath.Ext(path), ".uf2") {
		if data, err = binToUF2(data); err != nil {
			return err
		}
	}

	vol, err := findUF2Volume()
	if err != nil {
		return err
	}
	if vol == "" {
		return fmt.Errorf("the %s drive is not mounted.\n"+
			"Press reset twice quickly to bring it up, then try again", uf2VolumeLabel)
	}

	dest := filepath.Join(vol, "firmware.uf2")
	if err := os.WriteFile(dest, data, 0o644); err != nil {
		return err
	}
	// The bootloader reboots as soon as the last block lands, so the drive
	// disappearing mid-write is success, not a failure.
	fmt.Printf("copied %d blocks to %s\n", len(data)/uf2BlockSize, vol)
	return nil
}
