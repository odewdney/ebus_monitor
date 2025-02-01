# ebus_monitor
 

# Interactions

Boiler alone (Master 03, Slave 08):
    sends 64 b512 - 02 fe - *3 at startup
    sends 64 b512 - 02 - once 15 seconds from start up
    sends 64 b512 - 02 fe - *3 every 

responding to the 64 b512 - 02 packet on a combi boiler results in F.78 error




VR70

Req: 10 52 b523 (9) Data: 00

sensor: 0=VR10,1=VR11,2=demand
relay: 1=mixer
    r3 r5 s1 s2 s3 s4 s5 s6 
 1: 00 01 00 02 02 00 00 00 : ma-r3/4 s6->r5/6
 2: 00 01 00 00 02 00 00 00 : ma
 3: 00 01 00 00 00 00 00 00 : ma-r1 s6->r5/6
 4: 00 00 00 00 00 00 00 00 : --
 5: 01 01 00 02 02 00 00 00 : -- s5->r3/4 s6->r5/6
 6: 00 00 00 00 00 00 01 00 : ma-r3/4
 7: 00 00 00 00 00 00 00 00 : ma
 8: 00 00 00 00 00 00 00 00 : ma
 9: 00 00 00 00 00 00 00 00 : --
10: 00 00 00 00 00 00 00 00 : --
11: 00 00 00 00 00 00 00 00 : --
12: 00 01 00 00 00 00 01 00 : -- s6->r5/6

VR71

Req: 10 26 b523 (11) Data: 04 

sensor: 0=VR10,1=VR11,2=demand,3=PWM

                ss ss ss ss ss ss
                12 34 56 78 91 11
                             0 12
 1: 01 01 00 00 00 00 01 02 00 03
 2: 01 01 01 00 00 00 01 00 00 03
 3: 01 01 00 00 00 20 22 00 00 00
 6: 01 01 01 00 00 00 00 22 02 00

