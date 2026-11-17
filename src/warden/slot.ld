SECTIONS
{
  .note.warden : ALIGN(4)
  {
    KEEP(*(.note.warden))
  }
}
INSERT AFTER .note.gnu.build-id;
