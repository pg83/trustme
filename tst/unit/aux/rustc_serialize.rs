// Minimal legacy codec contract for the compiler's built-in derives.
// Field values and enum discriminants go through the fallible stream methods.
pub trait Encodable {
    fn encode<E: Encoder>(&self, encoder: &mut E) -> Result<(), E::Error>;
}

pub trait Decodable: Sized {
    fn decode<D: Decoder>(decoder: &mut D) -> Result<Self, D::Error>;
}

pub trait Encoder: Sized {
    type Error;
    fn emit_u64(&mut self, value: u64) -> Result<(), Self::Error>;

    fn emit_struct<F>(&mut self, _name: &str, _len: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        f(self)
    }

    fn emit_struct_field<F>(&mut self, _name: &str, _index: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        f(self)
    }

    fn emit_tuple_struct<F>(&mut self, _name: &str, _len: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        f(self)
    }

    fn emit_tuple_struct_arg<F>(&mut self, _index: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        f(self)
    }

    fn emit_enum<F>(&mut self, _name: &str, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        f(self)
    }

    fn emit_enum_variant<F>(&mut self, _name: &str, index: usize, _len: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        self.emit_u64(index as u64)?;
        f(self)
    }

    fn emit_enum_variant_arg<F>(&mut self, _index: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        f(self)
    }

    fn emit_enum_struct_variant<F>(&mut self, _name: &str, index: usize, _len: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        self.emit_u64(index as u64)?;
        f(self)
    }

    fn emit_enum_struct_variant_field<F>(&mut self, _name: &str, _index: usize, f: F) -> Result<(), Self::Error>
    where F: FnOnce(&mut Self) -> Result<(), Self::Error> {
        f(self)
    }
}

pub trait Decoder: Sized {
    type Error;
    fn read_u64(&mut self) -> Result<u64, Self::Error>;
    fn error(&mut self, message: &str) -> Self::Error;

    fn read_struct<T, F>(&mut self, _name: &str, _len: usize, f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self) -> Result<T, Self::Error> {
        f(self)
    }

    fn read_struct_field<T, F>(&mut self, _name: &str, _index: usize, f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self) -> Result<T, Self::Error> {
        f(self)
    }

    fn read_tuple_struct<T, F>(&mut self, _name: &str, _len: usize, f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self) -> Result<T, Self::Error> {
        f(self)
    }

    fn read_tuple_struct_arg<T, F>(&mut self, _index: usize, f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self) -> Result<T, Self::Error> {
        f(self)
    }

    fn read_enum<T, F>(&mut self, _name: &str, f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self) -> Result<T, Self::Error> {
        f(self)
    }

    fn read_enum_variant<T, F>(&mut self, _names: &[&str], f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self, usize) -> Result<T, Self::Error> {
        let index = self.read_u64()? as usize;
        f(self, index)
    }

    fn read_enum_variant_arg<T, F>(&mut self, _index: usize, f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self) -> Result<T, Self::Error> {
        f(self)
    }

    fn read_enum_struct_variant_field<T, F>(&mut self, _name: &str, _index: usize, f: F) -> Result<T, Self::Error>
    where F: FnOnce(&mut Self) -> Result<T, Self::Error> {
        f(self)
    }
}

impl Encodable for u8 {
    fn encode<E: Encoder>(&self, encoder: &mut E) -> Result<(), E::Error> {
        encoder.emit_u64(*self as u64)
    }
}

impl Decodable for u8 {
    fn decode<D: Decoder>(decoder: &mut D) -> Result<Self, D::Error> {
        Ok(decoder.read_u64()? as u8)
    }
}

impl Encodable for u16 {
    fn encode<E: Encoder>(&self, encoder: &mut E) -> Result<(), E::Error> {
        encoder.emit_u64(*self as u64)
    }
}

impl Decodable for u16 {
    fn decode<D: Decoder>(decoder: &mut D) -> Result<Self, D::Error> {
        Ok(decoder.read_u64()? as u16)
    }
}
