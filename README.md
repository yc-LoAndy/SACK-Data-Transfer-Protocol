# SACK Protocol
Implementation of SACK (Selective Acknowledgement) file transfer protocol: a hybrid version of two pipelined reliable data transfer protocols, Go-Back-N and Selective Repeat.

In SACK Protocol, there are two types of acknowledgment information simultaneously: **cumulative ACK** and **selective ACK**.
With different ACKs, the receiver can cache and selectively acknowledge out-of-order segments, and the sender, knowing that some of those segments have been selectively acknowledged, can utilize the bandwidth to retransmit other segments that haven’t been selectively acknowledged.
The original Go-Back-N mechanism can also be used to cumulatively acknowledge multiple segments.

## Congestion Control
Also, a congestion control scheme is implemented in the transmission, illustrated in the following finite state machine.
<p align="center">
  <img src="https://github.com/yc-LoAndy/SACK-Data-Transfer-Protocol/blob/main/congestion_control_fsm.png" alt="FSM" style="width: 700px">
</p>

The idea of this protocol originated from an option of TCP's error-recovery mechanism defined in [RFC2018](https://datatracker.ietf.org/doc/html/rfc2018). For full details of the protocol, please refer to [The Spec Sheet](https://github.com/yc-LoAndy/SACK-Data-Transfer-Protocol/blob/main/CN2023-HW3-ProblemSheet_1201.pdf).
