//! 

#[derive(Clone,Copy,Debug)]
pub struct Span(usize);
impl !Send for Span {}
impl !Sync for Span {}

static mut SPANS: Vec<Option<RealSpan>> = Vec::new();
static mut SPANS_COMPLETE: bool = false;
impl Span
{
    pub(crate) fn define(idx: usize, _parent: Option<Span>, source_file: SourceFile, lines: ::std::ops::Range<usize>, ofs: ::std::ops::Range<usize>) {
        let lh = unsafe {
            assert!(!SPANS_COMPLETE);
            &mut SPANS
            };
        while lh.len() <= idx {
            lh.push(None);
        }
        lh[idx] = Some(RealSpan {
            file: source_file,
            lines,
            ofs,
            context: idx,
            });
    }
    pub(crate) fn freeze_definitions() {
        unsafe { SPANS_COMPLETE = true; }
    }
    pub(crate) fn from_raw(idx: usize) -> Self {
        Span(idx)
    }
    /// The index the compiler knows this span by - echoed back so that a token
    /// this macro passes through keeps the resolution context it arrived with.
    /// A span this macro derived (`start()`, `end()`) answers with its parent's.
    pub(crate) fn to_raw(&self) -> usize {
        match self.entry() {
        Some(v) => v.context,
        None => self.0,
        }
    }
    fn entry(&self) -> Option<&'static RealSpan> {
        unsafe { assert!(SPANS_COMPLETE); SPANS.get(self.0).and_then(|e| e.as_ref()) }
    }
    /// The definition behind this span; the mixed site has none of its own and
    /// is located at the call site, as upstream's is.
    fn real(&self) -> &'static RealSpan {
        match self.entry().or_else(|| Span(1).entry()) {
        Some(v) => v,
        None => panic!("Undefined span #{}", self.0),
        }
    }
    /// An empty span at one position of this span's file, in this span's context.
    fn derived(&self, line: usize, ofs: usize) -> Span {
        let parent = self.real();
        let lh = unsafe { &mut SPANS };
        lh.push(Some(RealSpan {
            file: parent.file.clone(),
            lines: line..line,
            ofs: ofs..ofs,
            context: parent.context,
            }));
        Span(lh.len() - 1)
    }
}

impl Span
{
    /* Upstream's bridge is only set up while a procedural macro runs; a call from
       anywhere else panics with this message, and proc-macro2 0.4 probes exactly that
       (`catch_unwind(|| Span::call_site())`) to choose between compiler and fallback
       spans - a call that quietly answered gave it compiler spans whose `start()` is
       line 0 outside a proc macro (version-sync's `html_root_url` check). */
    fn require_available() {
        if !crate::is_available() {
            panic!("procedural macro API is used outside of a procedural macro");
        }
    }
    pub fn call_site() -> Span {
        Self::require_available();
        Span(1)
    }
    //pub fn def_site() -> Span {
    //    Span(1)
    //}
    // 1.45
    pub fn mixed_site() -> Span {
        Self::require_available();
        Span(0)
    }
    // 1.45
    pub fn resolved_at(&self, _other: Span) -> Span {
        Span(0)
    }
    // 1.45
    pub fn located_at(&self, _other: Span) -> Span {
        Span(0)
    }

    // 1.66
    pub fn source_text(&self) -> Option<String> {
        None
    }

    // Unstable at 1.54
    pub fn source_file(&self) -> SourceFile {
        self.real().file.clone()
    }

    // 1.88: an empty span directly before this one
    pub fn start(&self) -> Span {
        let v = self.real();
        self.derived(v.lines.start, v.ofs.start)
    }

    // 1.88: an empty span directly after this one
    pub fn end(&self) -> Span {
        let v = self.real();
        self.derived(v.lines.end, v.ofs.end)
    }

    // 1.88: the one-indexed line where this span starts
    pub fn line(&self) -> usize {
        self.real().lines.start
    }

    // 1.88: the one-indexed column where this span starts; the compiler's
    // offset within the line counts from zero
    pub fn column(&self) -> usize {
        self.real().ofs.start + 1
    }

    // 1.88: the source path for display
    pub fn file(&self) -> String {
        self.real().file.0.display().to_string()
    }

    // 1.88
    pub fn local_file(&self) -> Option<::std::path::PathBuf> {
        match unsafe { assert!(SPANS_COMPLETE); SPANS.get(self.0) } {
        Some(&Some(ref v)) => Some(v.file.path()),
        Some(&None) => None,
        _ => panic!("Undefined span #{}", self.0),
        }
    }
}

/// The inner definition of a span: its position, and the index the compiler
/// knows its resolution context by.
struct RealSpan {
    file: SourceFile,
    lines: ::std::ops::Range<usize>,
    ofs: ::std::ops::Range<usize>,
    context: usize,
}


// Unstable at 1.54
#[derive(Clone)]
pub struct SourceFile( pub(crate) ::std::path::PathBuf, pub(crate) bool );
impl SourceFile
{
    pub fn path(&self) -> ::std::path::PathBuf {
        self.0.clone()
    }
    pub fn is_real(&self) -> bool {
        self.1
    }
}
