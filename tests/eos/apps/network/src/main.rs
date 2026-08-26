#![feature(restricted_std)]

use std::io::{Read, Write};
use std::net::{Ipv4Addr, SocketAddrV4, TcpListener, TcpStream, ToSocketAddrs, UdpSocket};

fn main() -> std::io::Result<()> {
    let loopback = SocketAddrV4::new(Ipv4Addr::LOCALHOST, 0);
    let listener = TcpListener::bind(loopback)?;
    let listener_address = listener.local_addr()?;
    let server = std::thread::spawn(move || -> std::io::Result<()> {
        let (mut stream, peer) = listener.accept()?;
        assert!(peer.ip().is_loopback(), "TCP peer must use numeric loopback");
        let mut request = [0_u8; 3];
        stream.read_exact(&mut request)?;
        assert_eq!(&request, b"EOS", "TCP server must receive fixed bytes");
        stream.write_all(b"TCP")?;
        Ok(())
    });

    let mut client = TcpStream::connect(listener_address)?;
    client.write_all(b"EOS")?;
    let mut reply = [0_u8; 3];
    client.read_exact(&mut reply)?;
    assert_eq!(&reply, b"TCP", "TCP client must receive fixed bytes");
    server.join().expect("TCP server thread must not panic")?;
    println!("network tcp: numeric-loopback exchange=EOS/TCP");

    let first = UdpSocket::bind(loopback)?;
    let second = UdpSocket::bind(loopback)?;
    first.connect(second.local_addr()?)?;
    second.connect(first.local_addr()?)?;
    assert_eq!(first.send(b"EOS")?, 3);
    let mut datagram = [0_u8; 3];
    assert_eq!(second.recv(&mut datagram)?, 3);
    assert_eq!(&datagram, b"EOS", "UDP peer must receive fixed bytes");
    assert_eq!(second.send(b"UDP")?, 3);
    assert_eq!(first.recv(&mut datagram)?, 3);
    assert_eq!(&datagram, b"UDP", "UDP origin must receive fixed bytes");
    println!("network udp: numeric-loopback exchange=EOS/UDP");

    match ("localhost", 7).to_socket_addrs() {
        Err(error) if error.kind() == std::io::ErrorKind::Unsupported => {
            println!("network dns: Unsupported");
        }
        Err(error) => return Err(error),
        Ok(_) => panic!("EOS DNS probe must report Unsupported"),
    }
    Ok(())
}
