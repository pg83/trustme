// Extracted from library/std/src/os/unix/net/datagram.rs:616
Sends data and ancillary data on the socket.

On success, returns the number of bytes written.

Examples



#![feature(unix_socket_ancillary_data)]
use std::os::unix::net::{UnixDatagram, SocketAncillary};
use std::io::IoSlice;

fn main() -> std::io::Result<()> {
    let sock = UnixDatagram::unbound()?;
    let buf1 = [1; 8];
    let buf2 = [2; 16];
    let buf3 = [3; 8];
    let bufs = &[
        IoSlice::new(&buf1),
        IoSlice::new(&buf2),
        IoSlice::new(&buf3),
    ][..];
    let fds = [0, 1, 2];
    let mut ancillary_buffer = [0; 128];
    let mut ancillary = SocketAncillary::new(&mut ancillary_buffer[..]);
    ancillary.add_fds(&fds[..]);
    sock.send_vectored_with_ancillary(bufs, &mut ancillary)
        .expect("send_vectored_with_ancillary function failed");
    Ok(())
}
