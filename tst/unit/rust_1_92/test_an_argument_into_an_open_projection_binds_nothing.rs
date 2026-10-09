// `Pixel::from_channels(NumCast::from(..).unwrap(), ..)` whose `Self` comes
// only from the later `image.put_pixel(x, y, outpixel)`. rustc normalizes
// the formal `<?P as Pixel>::Subpixel` to a fresh variable, so coercing the
// `unwrap()` result, itself a variable, into it is a deferred `Coerce`
// predicate and binds nothing; `put_pixel`'s argument then gives `?P`. We took
// the variable source against the projection as a ready argument binding,
// and the binding cut held `put_pixel`'s argument behind a binding that could
// never be made. image's `huerotate_in_place`.
trait ToPrimitive {
    fn to_f64(&self) -> Option<f64>;
}

trait NumCast: Sized + ToPrimitive {
    fn from<T: ToPrimitive>(n: T) -> Option<Self>;
}

impl ToPrimitive for u8 {
    fn to_f64(&self) -> Option<f64> {
        Some(*self as f64)
    }
}

impl ToPrimitive for f64 {
    fn to_f64(&self) -> Option<f64> {
        Some(*self)
    }
}

impl NumCast for u8 {
    fn from<T: ToPrimitive>(n: T) -> Option<Self> {
        n.to_f64().map(|v| v as u8)
    }
}

impl NumCast for f64 {
    fn from<T: ToPrimitive>(n: T) -> Option<Self> {
        n.to_f64()
    }
}

trait Primitive: Copy + NumCast + PartialOrd {}

impl Primitive for u8 {}

trait Pixel: Copy {
    type Subpixel: Primitive;

    fn channels4(&self) -> (Self::Subpixel, Self::Subpixel, Self::Subpixel, Self::Subpixel);

    fn from_channels(a: Self::Subpixel, b: Self::Subpixel, c: Self::Subpixel, d: Self::Subpixel) -> Self;
}

#[derive(Clone, Copy, Debug, PartialEq)]
struct Rgba([u8; 4]);

impl Pixel for Rgba {
    type Subpixel = u8;

    fn channels4(&self) -> (u8, u8, u8, u8) {
        (self.0[0], self.0[1], self.0[2], self.0[3])
    }

    fn from_channels(a: u8, b: u8, c: u8, d: u8) -> Self {
        Rgba([a, b, c, d])
    }
}

trait GenericImageView {
    type Pixel: Pixel;

    fn dimensions(&self) -> (u32, u32);

    fn get_pixel(&self, x: u32, y: u32) -> Self::Pixel;
}

trait GenericImage: GenericImageView {
    fn put_pixel(&mut self, x: u32, y: u32, pixel: Self::Pixel);
}

struct Image(Vec<Rgba>, u32);

impl GenericImageView for Image {
    type Pixel = Rgba;

    fn dimensions(&self) -> (u32, u32) {
        (self.1, self.0.len() as u32 / self.1)
    }

    fn get_pixel(&self, x: u32, y: u32) -> Rgba {
        self.0[(y * self.1 + x) as usize]
    }
}

impl GenericImage for Image {
    fn put_pixel(&mut self, x: u32, y: u32, pixel: Rgba) {
        self.0[(y * self.1 + x) as usize] = pixel;
    }
}

fn clamp<N: PartialOrd>(a: N, min: N, max: N) -> N {
    if a < min {
        min
    } else if a > max {
        max
    } else {
        a
    }
}

fn brighten_in_place<I: GenericImage>(image: &mut I, factor: f64) {
    let (width, height) = image.dimensions();
    for y in 0..height {
        for x in 0..width {
            let pixel = image.get_pixel(x, y);
            let (k1, k2, k3, k4) = pixel.channels4();
            let vec: (f64, f64, f64, f64) = (
                NumCast::from(k1).unwrap(),
                NumCast::from(k2).unwrap(),
                NumCast::from(k3).unwrap(),
                NumCast::from(k4).unwrap(),
            );
            let max = 255f64;
            let outpixel = Pixel::from_channels(
                NumCast::from(clamp(vec.0 * factor, 0.0, max)).unwrap(),
                NumCast::from(clamp(vec.1 * factor, 0.0, max)).unwrap(),
                NumCast::from(clamp(vec.2 * factor, 0.0, max)).unwrap(),
                NumCast::from(clamp(vec.3, 0.0, max)).unwrap(),
            );
            image.put_pixel(x, y, outpixel);
        }
    }
}

fn main() {
    let mut image = Image(vec![Rgba([10, 100, 200, 255]), Rgba([1, 2, 3, 4])], 2);
    brighten_in_place(&mut image, 2.0);
    assert_eq!(image.0, vec![Rgba([20, 200, 255, 255]), Rgba([2, 4, 6, 4])]);
}
