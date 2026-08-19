/* EOS application layout, derived from MARTOS-SMP 14.0.39. */
ENTRY(main)

/*
 * This loader has no cross-image EHABI lookup hook, so libgcc must use the
 * retained application-local __exidx_start/__exidx_end table.  MARTOS uses
 * libc++abi rather than GNU libsupc++, so its absent optional type-match hook
 * keeps the weak-symbol zero value without becoming a dynamic relocation.
 */
PROVIDE(__gnu_Unwind_Find_exidx = 0);
PROVIDE(__cxa_type_match = 0);

PHDRS
{
  headers PT_PHDR PHDRS;
  interp PT_INTERP;
  text PT_LOAD FILEHDR PHDRS FLAGS(5);
  data PT_LOAD FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
  /* GNU ld scripts spell the ARM-specific PT_ARM_EXIDX value numerically. */
  exidx 0x70000001 FLAGS(4);
}

SECTIONS
{
  . += SIZEOF_HEADERS;

  .interp : { *(.interp) } :text :interp

  .text :
  {
    *(.freertos_vectors)
    *(.vectors)
    *(.boot)
    *(.text)
    *(.text.*)
    *(.gnu.linkonce.t.*)
    *(.plt)
    *(.gnu_warning)
    *(.glue_7)
    *(.glue_7t)
    *(.vfp11_veneer)
  } :text

  .init : { KEEP(*(.init)) } :text
  .fini : { KEEP(*(.fini)) } :text

  .rodata :
  {
    __rodata_start = .;
    *(.rodata)
    *(.rodata.*)
    *(.gnu.linkonce.r.*)
    __rodata_end = .;
  } :text

  .rodata1 :
  {
    __rodata1_start = .;
    *(.rodata1)
    *(.rodata1.*)
    __rodata1_end = .;
  } :text

  .gcc_except_table : { KEEP(*(.gcc_except_table*)) } :text
  .ARM.extab :
  {
    KEEP(*(.ARM.extab*))
    KEEP(*(.gnu.linkonce.armextab.*))
  } :text
  .ARM.exidx :
  {
    __exidx_start = .;
    KEEP(*(.ARM.exidx*))
    KEEP(*(.gnu.linkonce.armexidx.*))
    __exidx_end = .;
  } :text :exidx

  .eh_frame : { *(.eh_frame) } :text
  .eh_frame_hdr : { *(.eh_frame_hdr) } :text
  .dynsym : { *(.dynsym) } :text
  .dynstr : { *(.dynstr) } :text
  .hash : { *(.hash) } :text
  .gnu.hash : { *(.gnu.hash) } :text
  .rel.dyn : { *(.rel.dyn) *(.rel.dyn.*) } :text
  .rel.plt : { *(.rel.plt) *(.rel.plt.*) } :text

  . = ALIGN(CONSTANT(MAXPAGESIZE));

  .got : { *(.got) *(.got.*) *(.got.plt) } :data
  .data :
  {
    __data_start = .;
    *(.data)
    *(.data.*)
    *(.gnu.linkonce.d.*)
    *(.jcr)
    __data_end = .;
  } :data
  .data1 :
  {
    __data1_start = .;
    *(.data1)
    *(.data1.*)
    __data1_end = .;
  } :data
  .dynamic : { *(.dynamic) } :data :dynamic

  .preinit_array :
  {
    __preinit_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.preinit_array.*)))
    KEEP(*(.preinit_array))
    __preinit_array_end = .;
  } :data
  .init_array :
  {
    __init_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    __init_array_end = .;
  } :data
  .fini_array :
  {
    __fini_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    __fini_array_end = .;
  } :data

  .ctors :
  {
    __CTOR_LIST__ = .;
    ___CTORS_LIST___ = .;
    KEEP(*(EXCLUDE_FILE(*crtend.o) .ctors))
    KEEP(*(SORT(.ctors.*)))
    KEEP(*(.ctors))
    __CTOR_END__ = .;
    ___CTORS_END___ = .;
  } :data
  .dtors :
  {
    __DTOR_LIST__ = .;
    ___DTORS_LIST___ = .;
    KEEP(*(EXCLUDE_FILE(*crtend.o) .dtors))
    KEEP(*(SORT(.dtors.*)))
    KEEP(*(.dtors))
    __DTOR_END__ = .;
    ___DTORS_END___ = .;
  } :data

  .sdata : { *(.sdata) *(.sdata.*) *(.gnu.linkonce.s.*) } :data
  .sdata2 : { *(.sdata2) *(.sdata2.*) *(.gnu.linkonce.s2.*) } :data
  .sbss2 : { *(.sbss2) *(.sbss2.*) *(.gnu.linkonce.sb2.*) } :data

  .tdata : { *(.tdata) *(.tdata.*) *(.gnu.linkonce.td.*) } :data
  .tbss : { *(.tbss) *(.tbss.*) *(.gnu.linkonce.tb.*) } :data
  ASSERT(SIZEOF(.tdata) == 0 && SIZEOF(.tbss) == 0,
         "native TLS is unsupported")

  .bss :
  {
    __bss_start__ = .;
    *(.bss)
    *(.bss.*)
    *(.gnu.linkonce.b.*)
    *(COMMON)
    __bss_end__ = .;
  } :data
  _end = .;

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
