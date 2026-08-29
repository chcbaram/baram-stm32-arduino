package main

import (
	"fmt"
	"os"
	"strings"
	"time"
)

// progress reports how far a write has got.
//
// It has to work in two very different places. On a terminal, a carriage return
// rewrites the line in place and the result is a single bar that fills up. In
// the Arduino IDE's console, and anywhere else the output is a pipe, carriage
// returns are not interpreted and nothing is shown until a newline arrives - so
// the same code would print one long run of percentages, all at once, after the
// upload had already finished. There it prints a whole line per step instead,
// rarely enough not to be noise.
type progress struct {
	total     int
	tty       bool
	start     time.Time
	lastPct   int
	lastDraw  time.Time
	stepPct   int
}

func newProgress(total int) *progress {
	p := &progress{
		total:   total,
		tty:     isTerminal(os.Stdout),
		start:   time.Now(),
		lastPct: -1,
		stepPct: 10, // one line per 10% when we cannot redraw
	}
	return p
}

func isTerminal(f *os.File) bool {
	info, err := f.Stat()
	if err != nil {
		return false
	}
	return info.Mode()&os.ModeCharDevice != 0
}

func (p *progress) update(done int) {
	if p.total <= 0 {
		return
	}
	pct := done * 100 / p.total

	if p.tty {
		// Redrawing more than about 30 times a second is wasted work, but
		// always draw the last frame so the bar ends full.
		if pct == p.lastPct && done < p.total && time.Since(p.lastDraw) < 33*time.Millisecond {
			return
		}
		const width = 24
		filled := pct * width / 100
		fmt.Printf("\r  [%s%s] %3d%%",
			strings.Repeat("=", filled), strings.Repeat(" ", width-filled), pct)
		p.lastDraw = time.Now()
		p.lastPct = pct
		return
	}

	// Piped: only announce each step, and never the same one twice.
	step := pct / p.stepPct * p.stepPct
	if step > p.lastPct {
		fmt.Printf("  %3d%%\n", step)
		p.lastPct = step
	}
}

func (p *progress) done() {
	elapsed := time.Since(p.start)
	rate := float64(p.total) / 1024 / elapsed.Seconds()
	if p.tty {
		fmt.Printf("\r  [%s] 100%%  %.1f KB/s\n", strings.Repeat("=", 24), rate)
		return
	}
	if p.lastPct < 100 {
		fmt.Printf("  100%%\n")
	}
	fmt.Printf("  %d bytes in %.1fs, %.1f KB/s\n", p.total, elapsed.Seconds(), rate)
}
