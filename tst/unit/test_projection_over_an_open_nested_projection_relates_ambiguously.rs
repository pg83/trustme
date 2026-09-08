// A candidate relation of `<<Range<?T> as AsRangedCoord>::CoordDescType as Ranged>::ValueType`
// against `f64` is ambiguous while `?T` is open: the normalization goal on the nested
// projection is forced ambiguous, and its silence under the suppressing policy must not
// read as "no solution" - the closure `|&x| ..` handed to `x_label_formatter` would be
// refused the unsize coercion into `&dyn Fn(&X::ValueType)` before the range's element type
// is known (criterion `plot/plotters_backend/pdf.rs:62`).
use std::ops::Range;

pub trait DefaultValueFormatOption {}
pub struct DefaultFormatting;
impl DefaultValueFormatOption for DefaultFormatting {}

pub trait Ranged {
    type FormatOption: DefaultValueFormatOption;
    type ValueType;
    fn map(&self, value: &Self::ValueType, limit: (i32, i32)) -> i32;
    fn range(&self) -> Range<Self::ValueType>;
}

pub trait ValueFormatter<V> {
    fn format(_value: &V) -> String {
        panic!("unimplemented")
    }
    fn format_ext(&self, value: &V) -> String {
        Self::format(value)
    }
}

impl<R: Ranged<FormatOption = DefaultFormatting>> ValueFormatter<R::ValueType> for R
where
    R::ValueType: std::fmt::Debug,
{
    fn format(value: &R::ValueType) -> String {
        format!("{:?}", value)
    }
}

pub trait AsRangedCoord: Sized {
    type CoordDescType: Ranged<ValueType = Self::Value> + From<Self>;
    type Value;
}

#[derive(Clone)]
pub struct RangedCoordf64(f64, f64);

impl From<Range<f64>> for RangedCoordf64 {
    fn from(range: Range<f64>) -> Self {
        RangedCoordf64(range.start, range.end)
    }
}

impl Ranged for RangedCoordf64 {
    type FormatOption = DefaultFormatting;
    type ValueType = f64;
    fn map(&self, v: &f64, limit: (i32, i32)) -> i32 {
        let logic_length = (*v - self.0) / (self.1 - self.0);
        limit.0 + ((limit.1 - limit.0) as f64 * logic_length) as i32
    }
    fn range(&self) -> Range<f64> {
        self.0..self.1
    }
}

impl AsRangedCoord for Range<f64> {
    type CoordDescType = RangedCoordf64;
    type Value = f64;
}


#[derive(Clone)]
pub struct RangedCoordi32(i32, i32);

impl From<Range<i32>> for RangedCoordi32 {
    fn from(range: Range<i32>) -> Self {
        RangedCoordi32(range.start, range.end)
    }
}

impl Ranged for RangedCoordi32 {
    type FormatOption = DefaultFormatting;
    type ValueType = i32;
    fn map(&self, v: &i32, limit: (i32, i32)) -> i32 {
        limit.0 + (*v - self.0) * (limit.1 - limit.0) / (self.1 - self.0)
    }
    fn range(&self) -> Range<i32> {
        self.0..self.1
    }
}

impl AsRangedCoord for Range<i32> {
    type CoordDescType = RangedCoordi32;
    type Value = i32;
}

impl<T> AsRangedCoord for T
where
    T: Ranged,
{
    type CoordDescType = T;
    type Value = T::ValueType;
}

pub trait CoordTranslate {
    type From;
}

pub struct Cartesian2d<X: Ranged, Y: Ranged> {
    logic_x: X,
    logic_y: Y,
}

impl<X: Ranged, Y: Ranged> CoordTranslate for Cartesian2d<X, Y> {
    type From = (X::ValueType, Y::ValueType);
}

pub trait DrawingBackend {
    type ErrorType: std::fmt::Debug;
}

pub struct SvgBackend;
impl DrawingBackend for SvgBackend {
    type ErrorType = String;
}

pub struct ChartContext<'a, DB: DrawingBackend, CT: CoordTranslate> {
    backend: &'a DB,
    coord: CT,
}

pub struct ChartBuilder<'a, DB: DrawingBackend> {
    root: &'a DB,
}

impl<'a, DB: DrawingBackend> ChartBuilder<'a, DB> {
    pub fn on(root: &'a DB) -> Self {
        ChartBuilder { root }
    }

    pub fn margin(&mut self, _m: u32) -> &mut Self {
        self
    }

    pub fn build_cartesian_2d<X: AsRangedCoord, Y: AsRangedCoord>(
        &mut self,
        x_spec: X,
        y_spec: Y,
    ) -> Result<ChartContext<'a, DB, Cartesian2d<X::CoordDescType, Y::CoordDescType>>, DB::ErrorType> {
        Ok(ChartContext {
            backend: self.root,
            coord: Cartesian2d { logic_x: x_spec.into(), logic_y: y_spec.into() },
        })
    }
}

pub struct MeshStyle<'a, 'b, X: Ranged, Y: Ranged, DB: DrawingBackend> {
    parent: &'b ChartContext<'a, DB, Cartesian2d<X, Y>>,
    format_x: Option<&'b dyn Fn(&X::ValueType) -> String>,
    y_desc: Option<String>,
    x_desc: Option<String>,
    n_x_labels: usize,
    draw_x_mesh: bool,
}

impl<'a, DB, XT, YT, X, Y> ChartContext<'a, DB, Cartesian2d<X, Y>>
where
    DB: DrawingBackend,
    X: Ranged<ValueType = XT> + ValueFormatter<XT>,
    Y: Ranged<ValueType = YT> + ValueFormatter<YT>,
{
    pub fn configure_mesh(&mut self) -> MeshStyle<'a, '_, X, Y, DB> {
        MeshStyle::new(self)
    }
}

impl<'a, 'b, X, Y, XT, YT, DB> MeshStyle<'a, 'b, X, Y, DB>
where
    X: Ranged<ValueType = XT> + ValueFormatter<XT>,
    Y: Ranged<ValueType = YT> + ValueFormatter<YT>,
    DB: DrawingBackend,
{
    pub(crate) fn new(chart: &'b mut ChartContext<'a, DB, Cartesian2d<X, Y>>) -> Self {
        MeshStyle { parent: chart, format_x: None, y_desc: None, x_desc: None, n_x_labels: 10, draw_x_mesh: true }
    }
}

impl<'a, 'b, X, Y, DB> MeshStyle<'a, 'b, X, Y, DB>
where
    X: Ranged,
    Y: Ranged,
    DB: DrawingBackend,
{
    pub fn disable_mesh(&mut self) -> &mut Self {
        self.draw_x_mesh = false;
        self
    }

    pub fn x_labels(&mut self, value: usize) -> &mut Self {
        self.n_x_labels = value;
        self
    }

    pub fn x_label_formatter(&mut self, fmt: &'b dyn Fn(&X::ValueType) -> String) -> &mut Self {
        self.format_x = Some(fmt);
        self
    }

    pub fn x_desc<T: Into<String>>(&mut self, desc: T) -> &mut Self {
        self.x_desc = Some(desc.into());
        self
    }

    pub fn y_desc<T: Into<String>>(&mut self, desc: T) -> &mut Self {
        self.y_desc = Some(desc.into());
        self
    }

    pub fn draw(&mut self) -> Result<String, DB::ErrorType> {
        let x = self.parent.coord.logic_x.range().start;
        Ok(match self.format_x {
            Some(f) => f(&x),
            None => String::new(),
        })
    }
}

pub trait Zero {
    fn zero() -> Self;
}
impl Zero for f64 {
    fn zero() -> f64 {
        0.0
    }
}

pub fn fitting_range<'a, T: 'a, I: IntoIterator<Item = &'a T>>(iter: I) -> Range<T>
where
    T: Zero + PartialOrd + Clone,
{
    let (mut lb, mut ub) = (None, None);
    for value in iter.into_iter() {
        if lb.as_ref().map_or(true, |lbv: &T| lbv > value) {
            lb = Some(value.clone());
        }
        if ub.as_ref().map_or(true, |ubv: &T| ubv < value) {
            ub = Some(value.clone());
        }
    }
    lb.unwrap_or_else(T::zero)..ub.unwrap_or_else(T::zero)
}

fn pretty_print_float(x: f64, _trim: bool) -> String {
    format!("{:.2}", x)
}

fn pdf_figure(values: &[f64]) -> String {
    let mut xs = Vec::new();
    let x_range = fitting_range(xs.iter());
    let y_range = 0.0f64..1.0f64;

    let root_area = SvgBackend;
    let mut cb = ChartBuilder::on(&root_area);
    let mut chart = cb.margin(5).build_cartesian_2d(x_range, y_range.clone()).unwrap();

    let label = chart
        .configure_mesh()
        .disable_mesh()
        .x_label_formatter(&|&x| pretty_print_float(x, true))
        .x_labels(5)
        .draw()
        .unwrap();
    xs.push(values[0]);
    format!("{} {}", label, xs.len())
}

fn main() {
    assert_eq!(pdf_figure(&[1.5, 3.0]), "0.00 1");
}
