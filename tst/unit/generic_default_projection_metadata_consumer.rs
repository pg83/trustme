extern crate generic_default_projection_metadata_producer as producer;

struct Bytes;

impl producer::Reader for Bytes {
    type Offset = usize;
}

pub type Concrete = producer::Unit<Bytes>;
