// Since 1.90 rustc links x86_64-unknown-linux-gnu programs with lld: it runs
// `cc -fuse-ld=lld`, and lld records itself as `Linker: LLD ...` in the
// program's `.comment` section. The C backend linked with whatever linker the
// C++ driver picks by default, usually GNU ld, which records nothing there.
fn u16_at(data: &[u8], at: usize) -> usize {
    u16::from_le_bytes(data[at..at + 2].try_into().unwrap()) as usize
}

fn u32_at(data: &[u8], at: usize) -> usize {
    u32::from_le_bytes(data[at..at + 4].try_into().unwrap()) as usize
}

fn u64_at(data: &[u8], at: usize) -> usize {
    u64::from_le_bytes(data[at..at + 8].try_into().unwrap()) as usize
}

fn section<'a>(elf: &'a [u8], wanted: &[u8]) -> Option<&'a [u8]> {
    let headers = u64_at(elf, 0x28);
    let size = u16_at(elf, 0x3a);
    let count = u16_at(elf, 0x3c);
    let names = headers + u16_at(elf, 0x3e) * size;
    let names = &elf[u64_at(elf, names + 0x18)..];
    (0..count).map(|i| headers + i * size).find_map(|h| {
        let name = &names[u32_at(elf, h)..];
        let name = &name[..name.iter().position(|&b| b == 0).unwrap()];
        let start = u64_at(elf, h + 0x18);
        (name == wanted).then(|| &elf[start..start + u64_at(elf, h + 0x20)])
    })
}

fn main() {
    if !cfg!(all(target_arch = "x86_64", target_os = "linux", target_env = "gnu")) {
        return;
    }
    let exe = std::env::current_exe().unwrap();
    let elf = std::fs::read(&exe).unwrap();
    let comment = section(&elf, b".comment").unwrap();
    let mark = b"Linker: LLD";
    assert!(
        comment.windows(mark.len()).any(|w| w == mark),
        "{} was not linked by lld: {:?}",
        exe.display(),
        String::from_utf8_lossy(comment),
    );
}
