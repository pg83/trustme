// `IfdDecoder { inner: TagReader { decoder: &mut self.value_reader, ifd } }`
// with `inner: TagReader<'a, dyn EntryDecoder + 'a>` and `TagReader<'a, D:
// ?Sized> { decoder: &'a mut D, .. }`. rustc's `check_expr_struct` relates the
// literal's type to its expectation before it checks any field: the hint is
// `sup(expected, adt_ty)` under `fudge_inference_if_ok`, then
// `demand_eqtype(adt_ty_hint, adt_ty)`. `D` is `dyn EntryDecoder` when
// `decoder`'s value is checked, and `&mut ValueReader<R>` unsizes into it.
// Once a field's coercion came after its value, `D` was bound to
// `ValueReader<R>` from the field instead. tiff, under image.
use std::io::Read;

trait EntryDecoder {
    fn id(&self) -> u8;
}

struct ValueReader<R> {
    reader: R,
    id: u8,
}

impl<R: Read> EntryDecoder for ValueReader<R> {
    fn id(&self) -> u8 {
        self.id
    }
}

struct TagReader<'a, D: EntryDecoder + ?Sized> {
    decoder: &'a mut D,
    ifd: &'a u8,
}

struct IfdDecoder<'a> {
    inner: TagReader<'a, dyn EntryDecoder + 'a>,
}

struct Decoder<R> {
    value_reader: ValueReader<R>,
}

impl<R: Read> Decoder<R> {
    fn read_directory_tags<'ifd>(&'ifd mut self, ifd: &'ifd u8) -> IfdDecoder<'ifd> {
        IfdDecoder {
            inner: TagReader {
                decoder: &mut self.value_reader,
                ifd,
            },
        }
    }
}

fn main() {
    let mut decoder = Decoder { value_reader: ValueReader { reader: std::io::empty(), id: 5 } };
    let ifd = 3u8;
    let tags = decoder.read_directory_tags(&ifd);
    assert_eq!(tags.inner.decoder.id() + *tags.inner.ifd, 8);
    let _ = &decoder.value_reader.reader;
}
