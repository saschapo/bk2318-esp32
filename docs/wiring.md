# Wiring: B&K-SI socket, levels, ESP32-C6 adapter

Tags: [manual] · [forum] · [measured] · [hypothesis] · [conclusion]. Main source: manual pp. 15–25.

## B&K-SI socket pinout on the ZI 0054 [manual, pp. 22–23]

Fig. 4.5 is the **external** view, as you look at the front panel. Red key dot on top.

```text
              Red dot
                ▼
                1  EXTC
     HS  2              5  RD
        TD  3        4  SG
```

**Our unit** [measured, 2026-10-19]: five socket contacts, laid out as in fig. 4.5. Under the red dot is the key slot and pin 1; clockwise from there: 5 right, 4 lower right, 3 lower left, 2 left. Labelled photo: `evidence/printer_SN1941478/BK-SI_socket_pins_annotated.jpg` (made with `tools/annotate_socket.py`). Printer on, nothing connected: pin 3 (TD) to pin 4 (SG) = **−5.5 V**, an idle output, as expected.

| Pin | Signal | Direction at the printer [conclusion from figs. 4.7–4.10] |
|---:|---|---|
| 1 | EXTC, Externals Connected | output (to DSR/DCD/RI on a PC) |
| 2 | HS, Handshake, READY/BUSY | output (to CTS on a PC) |
| 3 | TD, Transmitted Data | **data out** (XON/XOFF come here) |
| 4 | SG, Signal Ground | ground |
| 5 | RD, Received Data | **data in** |
| shell | PG, Protective Ground | connector body |

- Mating plug: 5-pin LEMO, B&K part **JP 0510**. Cable B&K-SI ↔ B&K-SI: AO 0299 (TD and RD crossed, the rest straight) [manual].
- Cable to an IBM PC DB9 (fig. 4.10) [manual]: TD 3 → 2 (PC RXD); RD 5 ← 3 (PC TXD); HS 2 → 8 (CTS); SG 4 → 5; EXTC 1 → 6 + 9; shield to SG. stoneset's AO 0404 cable matches this.

## Levels [manual, p. 21]

| | B&K-SI | RS-232C |
|---|---|---|
| Open circuit | ±5 V nominal | ±12 V nominal |
| Minimum at full load | transmitter ±2 V, receiver ±1 V | transmitter ±5 V, receiver ±3 V |

- Two jumpers inside the ZI 0054 (fig. 4.4): **±5 V is the factory setting**; ±10 V is the jumpers moved four pins towards the **A** mark on the board. ±10 V drains the battery faster; use it only if needed.
- "Interface supply 10 V" in some listings means these jumpers. No external supply goes to the socket [manual].
- FrodeM [forum]: it should work with most RS-232 even in the ±5 V setting.
- An SP3232 works with the factory ±5 V setting [conclusion]: its receiver threshold is at most 2.4 V and the printer gives about ±5 V into one load; its transmitter gives about ±5.4 V against the printer's ±1 V minimum. Leave the jumpers alone.
- **Line polarity is standard RS-232** [manual, fig. 4.1, p. 15]: idle and logic 1 negative, start bit and 0 positive, LSB first. The text on p. 16 says the opposite, which contradicts the figure and the fact that the printer works with a normal PC serial port. Trust the figure. This is exactly what an SP3232/MAX3232 gives with a non-inverted UART.

## ESP32-C6 through an SP3232 module

```text
ESP32-C6            SP3232 module                 B&K-SI socket
3V3  ───────────── VCC (pin 16)
GND  ───────────── GND (pin 15) ───────────────── 4  SG
GPIO2 TX ───────── T1IN (11)  →  T1OUT (14) ───── 5  RD (printer input)
GPIO3 RX ───────── R1OUT (12) ←  R1IN  (13) ───── 3  TD (printer output)
                                  leave open:     1 EXTC, 2 HS
```

- Board: **MuseLab nanoESP32-C6**. UART1 on **GPIO2 (TX) and GPIO3 (RX)**: broken out and not used at boot. Avoid strapping pins 4, 5, 8 (NeoPixel), 9 (BOOT), 15; USB 12, 13; console 16, 17.
- Power the module **from 3V3 only**: the receiver output swings to VCC and the C6 input does not tolerate 5 V.
- `Serial1.begin(1200, SERIAL_8N1, RX_PIN, TX_PIN);` with no inversion: the module inverts.
- Our module ("V0017") [measured, 2026-10-19]: the TTL labels were right (ESP TX → module "RXD", ESP RX ← module "TXD"); idle RS232 TXD = −6 V. A TXD–RXD loopback passed (`evidence/sp3232_module_V0017/loop_2026-10-19.log`). Check yours with a meter: SP3232 has the MAX3232 pinout.
- HS (READY/BUSY) is not needed: XON/XOFF is on. If you want it, use the chip's second receiver: R2IN pin 8, R2OUT pin 9.
- **Never connect an ESP32 pin straight to pin 3**: it idles at −5 V.

## LEMO plug

- An original LEMO plug costs more than the printer did. We probe the socket with resistor legs for now.
- The socket's inner diameter is **7 mm** [measured], which makes it series **0B** (0B sockets have an M9 body thread with about 7 mm inside; 1B have M12 with about 10 mm inside) [conclusion]. Plug: **FGG.0B.305** (5 pins, red-dot key). To be sure: an outer body or panel nut of about 9 mm means 0B, about 12 mm means 1B.
- We ordered a LEMO-compatible **FGG.0B.305.CLAD52Z** (about $3, solder pins, collet 52 for 4.1–5.0 mm cable; 32 is 2.1–3.0 mm, 42 is 3.1–4.0 mm). Fit to the original socket is not checked yet [hypothesis]. When soldering, remember the pinout above is the socket from outside; the plug's solder side is mirrored. Check the numbers moulded into the insulator.
- A ready B&K cable with the plug (AO 0299, AO 0404, AO 0335) is another option.

## Probing the socket

A square Dupont pin (0.64 mm, about 0.9 mm across the diagonal) will spread a socket contact made for about 0.7 mm. Use round pins: resistor legs (0.5–0.6 mm) or turned-pin socket pins, each in heat-shrink. Pins 3, 4 and 5 are close together; don't short them.

## B&K cables (reference)

| Cable | Ends | Use |
|---|---|---|
| AO 0299 | B&K-SI ↔ B&K-SI | 4436, 1213/WH1791, 3361/4419 → 2318 |
| AO 0335 | LEMO 5 → DB25 | 2231/ZI 9101 ↔ computer |
| AO 0404 | 9-pin D-sub ↔ LEMO | 2236 → 2318, PC → 2318 |
| AO 0406 | ? ↔ LEMO | 2260 → 2318 |
| AC 0064 | bare cable | for home-made RS-232 cables (pp. 24–25) |
