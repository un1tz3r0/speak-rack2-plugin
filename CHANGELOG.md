# Changelog

## 2.6.0
- speak-lite: a row of knob readouts along the bottom of the screen (PITCH, SPEED, VOICE, RANGE, FORMANT, each in
  its knob's colour, knob + CV), joined to the knobs by guide lines; the text area shows 6 lines.
- SAY is replaced by a small trigger button beside each of LINE and WORD (joined to its jack by a short rule) that
  acts as a trigger into that jack, on both speak-full and speak-lite. Ctrl+Enter still says the cursor's line.
- LINE / WORD names are centred on their jacks, between the TRIG. INCR. (above) and INDEX (below) lights.
- speak-lite: RESET moves up beside ENGINE, which sits right of centre in the middle row.

## 2.5.0
- speak-pix: speaker grille on the lower half of the panel.
- speak-lite: pastel knob caps (PITCH teal, SPEED cyan, VOICE amber, RANGE purple, FORMANT pink), an INPUTS
  heading over LINE and WORD, and the play button is now labelled SAY (on speak-full too).

## 2.4.0
- Modules renamed speak-full, speak-lite, speak-out and speak-pix (slugs unchanged, so patches still load).
- speak-lite: all five knobs in one row; LINE and WORD stacked bottom-left with larger labels and indicators;
  SAY and RESET in the middle, ENGINE above OUT; one VOICE CV spans every voice of both engines.

## 2.3.0
- LINE and WORD jacks replace TRIG/INDEX and the AUTO/WORD latches: triggers step, steady CV selects, with
  TRIG. INCR. / INDEX lights; with both patched LINE picks the line and WORD speaks its words.
- speak-lite: edit buttons and encoders removed for a 7-line full-width display.
- The display follows playback (and what's queued) while it doesn't have keyboard focus.

## 2.2.0
- speak-out expander: line/word/phoneme gates and indices, end-of-line trigger, envelope.
- Auto-increment, word mode (each word rendered on its own) and RESET.
- speak-lite merges engine pitch/rate with live PITCH/SPEED, with calibrated engine curves and a nearest-cached
  fallback so modulation never waits on a render.
- speak-pix: face picker moved to the menu, word captions; expanders chain in any order.

## 2.1.0
- speak-lite (16HP, no panel keyboard) and the speak-pix lip-sync face expander.

## 2.0.0
- First version: eSpeak NG and Flite, OLED editor with panel keyboard, TD-PSOLA pitch/formant/speed.
