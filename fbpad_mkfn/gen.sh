#!/bin/sh
# Generate fbpad fonts

FP="/root/klec/cgi/terminus-ttf-4.47.0"
OP="-h20 -w12"
SZ="16.5h135v125r-3"
./mkfn_stb $OP $FP/TerminusTTF-4.47.0.ttf:$SZ		>../ar.tf
./mkfn_stb $OP $FP/TerminusTTF-Italic-4.47.0.ttf:$SZ	>../ai.tf
./mkfn_stb $OP $FP/TerminusTTF-Bold-4.47.0.ttf:$SZ	>../ab.tf
