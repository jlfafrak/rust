#![feature(restricted_std)]

use std::io::{Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4, TcpListener, TcpStream, ToSocketAddrs, UdpSocket};
use std::time::Duration;

fn main() -> std::io::Result<()> {
    let loopback = SocketAddrV4::new(Ipv4Addr::LOCALHOST, 0);
    let listener = TcpListener::bind(loopback)?;
    listener.set_nonblocking(true)?;
    let _listener_address = listener.local_addr()?;

    let mut addresses = ("localhost", 7).to_socket_addrs()?;
    if let Some(address) = addresses.next() {
        let _ = TcpStream::connect_timeout(&address, Duration::from_millis(1)).map(
            |mut stream| {
                let _ = stream.set_read_timeout(Some(Duration::from_millis(1)));
                let _ = stream.set_write_timeout(Some(Duration::from_millis(1)));
                let _ = stream.write_all(b"EOS");
                let mut reply = [0_u8; 3];
                let _ = stream.read(&mut reply);
            },
        );
    }

    let udp = UdpSocket::bind(loopback)?;
    udp.set_nonblocking(true)?;
    udp.connect(SocketAddrV4::new(Ipv4Addr::LOCALHOST, 9))?;
    let _ = udp.send(b"EOS");
    let mut datagram = [0_u8; 16];
    let _ = udp.recv(&mut datagram);
    Ok(())
}
