pub fn fill_bytes(_: &mut [u8]) {
    panic!("EOS v1 does not provide cryptographically secure random bytes");
}

pub fn hashmap_random_keys() -> (u64, u64) {
    let mut key0 = 0;
    let mut key1 = 0;
    let rc = unsafe { libc::eos_hash_seed(&mut key0, &mut key1) };
    assert_eq!(rc, 0, "EOS hash seed initialization failed");
    (key0, key1)
}
