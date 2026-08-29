package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

// UF2, the format the bootloader accepts as a file copied onto the mass storage
// volume it exposes after a reset double-tap. Constants come from the
// bootloader's uf2.h and uf2_def.h and have to match.
const (
	uf2MagicStart0 = 0x0A324655 // "UF2\n"
	uf2MagicStart1 = 0x9E5D5157
	uf2MagicEnd    = 0x0AB16F30

	uf2FlagNoFlash  = 0x00000001
	uf2FlagFamilyID = 0x00002000

	uf2BlockSize   = 512
	uf2PayloadSize = 256

	// Custom, and not in Microsoft's uf2families.json. The bootloader rejects
	// any block whose familyID differs, which is what stops a UF2 built for
	// another board from being written here.
	uf2FamilyID = 0xFFFF0004

	// UF2_MAX_FW_SIZE in the bootloader.
	uf2MaxFwSize = 2 * 1024 * 1024

	// The FAT volume label the bootloader presents, used to find the drive.
	uf2VolumeLabel = "H750BOOT"
)

// binToUF2 converts a raw image into UF2 blocks.
//
// Target addresses are offsets from zero, not absolute addresses: the
// bootloader adds the application's base itself (uf2FlashWrite adds
// FLASH_ADDR_FIRM + FLASH_SIZE_TAG, i.e. 0x90001000). This matches
// `uf2conv.py --base 0x0`.
func binToUF2(image []byte) ([]byte, error) {
	if len(image) == 0 {
		return nil, fmt.Errorf("image is empty")
	}
	if len(image) > uf2MaxFwSize {
		return nil, fmt.Errorf("image is %d bytes, more than the %d the bootloader accepts over UF2",
			len(image), uf2MaxFwSize)
	}

	numBlocks := (len(image) + uf2PayloadSize - 1) / uf2PayloadSize
	out := make([]byte, 0, numBlocks*uf2BlockSize)

	for i := 0; i < numBlocks; i++ {
		off := i * uf2PayloadSize
		n := uf2PayloadSize
		if off+n > len(image) {
			n = len(image) - off
		}

		var b [uf2BlockSize]byte
		put := func(pos int, v uint32) { binary.LittleEndian.PutUint32(b[pos:], v) }
		put(0, uf2MagicStart0)
		put(4, uf2MagicStart1)
		put(8, uf2FlagFamilyID)
		put(12, uint32(off))
		put(16, uint32(n))
		put(20, uint32(i))
		put(24, uint32(numBlocks))
		put(28, uf2FamilyID)
		copy(b[32:], image[off:off+n])
		put(uf2BlockSize-4, uf2MagicEnd)

		out = append(out, b[:]...)
	}
	return out, nil
}

func writeUF2(inPath, outPath string) error {
	image, err := os.ReadFile(inPath)
	if err != nil {
		return err
	}
	blocks, err := binToUF2(image)
	if err != nil {
		return err
	}
	if err := os.WriteFile(outPath, blocks, 0o644); err != nil {
		return err
	}
	fmt.Printf("%s: %d bytes -> %s, %d blocks\n",
		filepath.Base(inPath), len(image), filepath.Base(outPath), len(blocks)/uf2BlockSize)
	return nil
}

// findUF2Volume locates the bootloader's mass storage volume by its label.
// Returns an empty path, and no error, when the board is not in that mode.
func findUF2Volume() (string, error) {
	var roots []string
	switch runtime.GOOS {
	case "darwin":
		roots = []string{"/Volumes"}
	case "linux":
		roots = []string{"/media", "/run/media", "/mnt"}
	case "windows":
		// Drive letters carry the label, so walk them directly.
		for c := 'D'; c <= 'Z'; c++ {
			p := string(c) + ":\\"
			if isUF2Volume(p) {
				return p, nil
			}
		}
		return "", nil
	}

	for _, root := range roots {
		entries, err := os.ReadDir(root)
		if err != nil {
			continue
		}
		for _, e := range entries {
			p := filepath.Join(root, e.Name())
			if strings.EqualFold(e.Name(), uf2VolumeLabel) && isUF2Volume(p) {
				return p, nil
			}
			// One level down, which is where Linux mounts per-user volumes.
			if sub, err := os.ReadDir(p); err == nil {
				for _, s := range sub {
					q := filepath.Join(p, s.Name())
					if strings.EqualFold(s.Name(), uf2VolumeLabel) && isUF2Volume(q) {
						return q, nil
					}
				}
			}
		}
	}
	return "", nil
}

// isUF2Volume checks for the INFO_UF2.TXT that every UF2 bootloader presents,
// so a same-named drive that is not the board is not mistaken for it.
func isUF2Volume(path string) bool {
	if _, err := os.Stat(filepath.Join(path, "INFO_UF2.TXT")); err == nil {
		return true
	}
	if _, err := os.Stat(filepath.Join(path, "INFO_UF2.txt")); err == nil {
		return true
	}
	return false
}
