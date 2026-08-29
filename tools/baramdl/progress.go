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
// upload had already finished. There it prints a whole line per step instead.
//
// The bar is always drawn, even for a write that finishes in a tenth of a
// second. Seeing it fill is how you know the upload is running rather than
// stuck, and that is worth more than the few lines it costs.
type progress struct {
	total    int
	tty      bool
	start    time.Time
	lastPct  int
	lastDraw time.Time
	stepPct  int
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
		fmt.Printf("\r  %s %3d%%", bar(pct), pct)
		p.lastDraw = time.Now()
		p.lastPct = pct
		return
	}

	// Piped: a bar per step, never the same step twice. It cannot be redrawn in
	// place - the Arduino IDE's console does not interpret carriage returns and
	// holds a line until a newline arrives - so the bar grows down the console
	// rather than across one line. That keeps it live, which is the half that
	// matters while waiting.
	// Skip the empty bar: 0% says nothing that "writing ..." has not already.
	step := pct / p.stepPct * p.stepPct
	if step > 0 && step > p.lastPct {
		fmt.Printf("  %s %3d%%\n", bar(step), step)
		p.lastPct = step
	}
}

func (p *progress) done() {
	elapsed := time.Since(p.start)
	rate := float64(p.total) / 1024 / elapsed.Seconds()
	// Finish the bar rather than leaving it part-drawn.
	if p.tty {
		fmt.Printf("\r  %s 100%%\n", bar(100))
	} else if p.lastPct < 100 {
		fmt.Printf("  %s 100%%\n", bar(100))
	}
	fmt.Printf("  %d bytes in %.1fs, %.1f KB/s\n", p.total, elapsed.Seconds(), rate)
}

// bar renders a fixed-width progress bar.
const barWidth = 24

func bar(pct int) string {
	filled := pct * barWidth / 100
	if filled > barWidth {
		filled = barWidth
	}
	return "[" + strings.Repeat("=", filled) + strings.Repeat(" ", barWidth-filled) + "]"
}
