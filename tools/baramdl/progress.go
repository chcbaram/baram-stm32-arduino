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
// Either way nothing is printed until the write has been going for a moment. A
// typical sketch is written in about a tenth of a second, and a progress report
// for something already finished is just noise; the summary line says all there
// is to say. Progress only earns its space when there is actually a wait.
type progress struct {
	total    int
	tty      bool
	start    time.Time
	lastPct  int
	lastDraw time.Time
	stepPct  int
	started  bool
}

// How long a write has to be running before it is worth reporting on.
const progressAfter = 400 * time.Millisecond

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
	// Stay quiet until this is slow enough to be worth narrating.
	if !p.started {
		if time.Since(p.start) < progressAfter {
			return
		}
		p.started = true
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
	step := pct / p.stepPct * p.stepPct
	if step > p.lastPct {
		fmt.Printf("  %s %3d%%\n", bar(step), step)
		p.lastPct = step
	}
}

func (p *progress) done() {
	elapsed := time.Since(p.start)
	rate := float64(p.total) / 1024 / elapsed.Seconds()
	if p.started {
		// Finish the bar rather than leaving it part-drawn.
		if p.tty {
			fmt.Printf("\r  %s 100%%\n", bar(100))
		} else if p.lastPct < 100 {
			fmt.Printf("  %s 100%%\n", bar(100))
		}
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
