
#![feature(lang_items)]
trait Hash<H> {
    fn hash2(&self, hasher: &H) -> u64;
}

trait Stream {
    fn input(&mut self, bytes: &[u8]);
    fn result(&self) -> u64;
}

trait StreamHasher {
    type S: Stream;
    fn stream(&self) -> Self::S;
}

//////////////////////////////////////////////////////////////////////////////

trait StreamHash<H: StreamHasher>: Hash<H> {
    fn input_stream(&self, stream: &mut H::S);
}

impl<H: StreamHasher> Hash<H> for u8 {
    fn hash2(&self, hasher: &H) -> u64 {
        let mut stream = hasher.stream();
        <u8 as StreamHash<H>>::input_stream(self, &mut stream);
        Stream::result(&stream)
    }
}

impl<H: StreamHasher> StreamHash<H> for u8 {
    fn input_stream(&self, stream: &mut H::S) {
        Stream::input(stream, &[*self]);
    }
}

fn main() {}
