\# Links to and information about the hardware used in this project



Because your ideal diode circuit operates on a low-voltage 5V rail, the golden rule for choosing a P-Channel MOSFET is selecting a "Logic-Level" variant. [1] 
Standard MOSFETs require a Gate-to-Source voltage ($V_{GS}$) of -10V to fully open, whereas a logic-level MOSFET turns fully on with just -4.5V or -3.3V. This guarantees that your 5V USB line will drive the transistor's on-state resistance ($R_{DS(on)}$) down to its absolute minimum, dropping mere millivolts across the channel. [2, 3, 4] 
Since you are prototyping using breadboards or through-hole components, these primary through-hole (TO-220 package) P-Channel MOSFETs are great fits for a 5V ideal diode:
------------------------------
[NDP6020P (Best Overall Choice)](https://www.google.com/search?q=onsemi+P-Channel+MOSFET+24+A+NDP6020P&ibp=oshop&pvorigin=29&prds=catalogid:14557815756944860195,productid:15311757146654564799,imageDocid:2532358730572184897,gpcid:6693254488947996770,pvt:hg,pvo:29,headlineOfferDocid:3895339928143157876)

* Signature Strength: Designed specifically for low-voltage applications.
* Specs at 5V: It features an incredibly low $R_{DS(on)}$ of roughly 50 mΩ at a 4.5V gate drive.
* The Math: At your ESP32's peak 500mA load, the voltage drop is a microscopic 0.025V (25 millivolts)—practically an absolute zero loss compared to a Schottky diode's 330mV drop.

------------------------------
[FQP27P06 (The Powerhouses)](https://www.google.com/search?q=Fqp27p06+P-channel+Mosfet+60v+27a&ibp=oshop&pvorigin=29&prds=catalogid:13229336417413594600,productid:10248559598986471839,imageDocid:5828234884817878364,gpcid:8576203746295923748,pvt:hg,pvo:29,headlineOfferDocid:4594147241674165185)

* Signature Strength: Massively robust and highly accessible in multi-packs online.
* Specs at 5V: While optimized for a -10V drive (where its resistance drops to 70 mΩ), it will comfortably crack open enough at a 5V gate-to-source threshold to pass up to 5 Amps without breaking a sweat or generating any noticeable heat.
* The Math: Under a 500mA load, the voltage drop stays entirely negligible (sub-40mV). It can be found via suppliers like [Newark](https://www.newark.com/onsemi/fqp27p06/mosfet-p-to-220-tube-50/dp/58K1524).

------------------------------
[SUP90P06-09L (High Efficiency Choice)](https://www.google.com/search?q=Vishay+MOSFET+SUP90P06-09L-E3&ibp=oshop&pvorigin=29&prds=catalogid:15360325970741621647,productid:17795681594952772090,imageDocid:14368634960761168858,gpcid:17978908302967774003,pvt:hg,pvo:29,headlineOfferDocid:3464428724667139757)

* Signature Strength: Extreme performance with an ultra-low threshold.
* Specs at 5V: The "L" in the part number denotes logic-level optimization. It drops down to a microscopic 11 mΩ at a -4.5V gate drive.
* The Math: It cuts your voltage drop down to under 6 millivolts. This is complete overkill for an ESP32, but if you want the absolute highest engineering efficiency on a 5V line, this handles it cleanly. [5, 6, 7, 8, 9] 

------------------------------
When wiring your chosen TO-220 P-Channel MOSFET up on a breadboard, it can look "backward" relative to a standard power switch because we are leveraging its internal body diode to start up the circuit: [10] 

       USB-C 5V VBUS ───>─── Source [Pin 2]
                               │ 
                       ┌───────┴───────┐
                       │  P-MOSFET     │────── Drain [Pin 3] ───>─── To Regulator Input
                       └───────┬───────┘
                               │
                             Gate [Pin 1]
                               │
                               ├──[ 100kΩ Resistor ]──┐
                               │                      │
       GND ────────────────────┴──────────────────────┴────────────────────── GND


   1. Source (Pin 2): Connect directly to your incoming USB-C 5V VBUS wire.
   2. Drain (Pin 3): Connect directly to the Input pin of your onboard switch-mode regulator.
   3. Gate (Pin 1): Connect a 100kΩ resistor between the Gate pin and your system Ground line. [10] 

How it works seamlessly: When you plug in the USB cable, 5V passes through the MOSFET's internal substrate body diode to feed your regulator. Simultaneously, the 100kΩ resistor forces the Gate down to 0V. Because the Source is sitting at 5V and the Gate is at 0V, the Gate-to-Source differential ($V_{GS}$) is a solid -5V. This triggers the logic-level threshold, forcing the transistor's main silicon channel wide open and bypassing the body diode to drop the power losses down to practically zero. [4, 10, 11] 
Would you like me to map out the pinout positions (Left, Center, Right) specifically for the TO-220 package option you choose, or do you have a P-MOSFET variant from an on-hand parts bin you want to verify compatibility for first?

[1] [https://www.origin-ic.com](https://www.origin-ic.com/blog/how-to-select-p-channel-mosfet-for-project/48704)
[2] [https://forum.dronebotworkshop.com](https://forum.dronebotworkshop.com/electronic-components/wanted-p-mosfet-with-low-rds-on-and-logic-level-drive-capability/)
[3] [https://www.youtube.com](https://www.youtube.com/watch?v=KgRJbThgGTY)
[4] [https://www.youtube.com](https://www.youtube.com/watch?v=HaSKZSuppZE&t=355)
[5] [https://protosupplies.com](https://protosupplies.com/product/fqp27p06/)
[6] [https://www.analog.com](https://www.analog.com/en/resources/technical-articles/primer-on-powerpath-controllers-ideal-diodes-prioritizers.html)
[7] [https://grobotronics.com](https://grobotronics.com/mosfet-p-channel-60v-27a-fqp27p06.html?sl=en)
[8] [https://www.youtube.com](https://www.youtube.com/watch?v=qpKr8nSzyZM)
[9] [https://www.youtube.com](https://www.youtube.com/watch?v=e19derT5z9A)
[10] [https://electronics.stackexchange.com](https://electronics.stackexchange.com/questions/223935/understanding-an-ideal-diode-made-from-a-p-channel-mosfet-and-pnp-transistors)
[11] [https://www.origin-ic.com](https://www.origin-ic.com/blog/how-to-select-p-channel-mosfet-for-project/48704)
