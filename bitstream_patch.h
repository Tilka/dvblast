static inline void psi_copy(uint8_t *pp_dest, uint8_t *pp_src)
{
     memcpy(pp_dest, pp_src, (PSI_MAX_SIZE + PSI_HEADER_SIZE) * sizeof(uint8_t));
}
