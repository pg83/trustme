// Extracted from library/std/src/os/unix/net/datagram.rs:431
Receives data and ancillary data from socket.

On success, returns the number of bytes read and if the data was truncated.

Examples



#![feature(unix_socket_ancillary_data)]
use std::os::unix::net::{UnixDatagram, SocketAncillary, AncillaryData};
use std::io::IoSliceMut;

fn main() -> std::io::Result<()> {
    let sock = UnixDatagram::unbound()?;
    let mut buf1 = [1; 8];
    let mut buf2 = [2; 16];
    let mut buf3 = [3; 8];
    let mut bufs = &mut [
        IoSliceMut::new(&mut buf1),
        IoSliceMut::new(&mut buf2),
        IoSliceMut::new(&mut buf3),
    ][..];
    let mut fds = [0; 8];
    let mut ancillary_buffer = [0; 128];
    let mut ancillary = SocketAncillary::new(&mut ancillary_buffer[..]);
    let (size, _truncated) = sock.recv_vectored_with_ancillary(bufs, &mut ancillary)?;
    println!("received {size}");
    for ancillary_result in ancillary.messages() {
        if let AncillaryData::ScmRights(scm_rights) = ancillary_result.unwrap() {
            for fd in scm_rights {
                println!("receive file descriptor: {fd}");
            }
        }
    }
    Ok(())
}
