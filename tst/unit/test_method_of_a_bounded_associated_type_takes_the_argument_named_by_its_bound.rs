// `section.sh_info(endian)` with `section: &Elf::SectionHeader`, the method declared by the
// bound of the associated type (`FileHeader::SectionHeader: SectionHeader<Elf = Self, Endian =
// Self::Endian>`) and the argument `Elf::Endian`: the method's `Self::Endian` is `Elf::Endian` by
// that bound, and the call resolves - also when the receiver and the argument are known when the
// call is first met (a field of a known receiver is resolved on the spot).
// (object `read/elf/comdat.rs:132`, seen through addr2line, insta, pest and syn 0.15)
use std::fmt::Debug;

pub trait Endian: Copy + Debug {
    fn read_u32(self, raw: [u8; 4]) -> u32;
}

#[derive(Clone, Copy, Debug)]
pub struct LittleEndian;

impl Endian for LittleEndian {
    fn read_u32(self, raw: [u8; 4]) -> u32 {
        u32::from_le_bytes(raw)
    }
}

pub trait FileHeader: Debug {
    type Endian: Endian;
    type SectionHeader: SectionHeader<Elf = Self, Endian = Self::Endian>;
    fn endian(&self) -> Self::Endian;
}

pub trait SectionHeader: Debug {
    type Elf: FileHeader<SectionHeader = Self, Endian = Self::Endian>;
    type Endian: Endian;
    fn sh_info(&self, endian: Self::Endian) -> u32;
}

#[derive(Debug)]
pub struct FileHeader32 {
    endian: LittleEndian,
}

#[derive(Debug)]
pub struct SectionHeader32 {
    sh_info: [u8; 4],
}

impl FileHeader for FileHeader32 {
    type Endian = LittleEndian;
    type SectionHeader = SectionHeader32;
    fn endian(&self) -> LittleEndian {
        self.endian
    }
}

impl SectionHeader for SectionHeader32 {
    type Elf = FileHeader32;
    type Endian = LittleEndian;
    fn sh_info(&self, endian: LittleEndian) -> u32 {
        endian.read_u32(self.sh_info)
    }
}

pub struct ElfFile<'data, Elf: FileHeader> {
    header: &'data Elf,
    endian: Elf::Endian,
}

pub struct ElfComdat<'data, 'file, Elf: FileHeader> {
    file: &'file ElfFile<'data, Elf>,
    section: &'data Elf::SectionHeader,
}

pub struct SymbolIndex(pub usize);

pub trait ObjectComdat {
    fn symbol(&self) -> SymbolIndex;
}

impl<'data, 'file, Elf: FileHeader> ObjectComdat for ElfComdat<'data, 'file, Elf> {
    fn symbol(&self) -> SymbolIndex {
        SymbolIndex(self.section.sh_info(self.file.endian) as usize)
    }
}

fn main() {
    let header = FileHeader32 { endian: LittleEndian };
    let file = ElfFile { header: &header, endian: header.endian() };
    let section = SectionHeader32 { sh_info: 7u32.to_le_bytes() };
    let comdat = ElfComdat { file: &file, section: &section };
    assert_eq!(comdat.symbol().0, 7);
    let _ = comdat.file.header;
}
