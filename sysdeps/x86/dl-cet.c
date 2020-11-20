/* x86 CET initializers function.
   Copyright (C) 2018-2020 Free Software Foundation, Inc.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include <unistd.h>
#include <errno.h>
#include <libintl.h>
#include <ldsodefs.h>
#include <dl-cet.h>

/* GNU_PROPERTY_X86_FEATURE_1_IBT and GNU_PROPERTY_X86_FEATURE_1_SHSTK
   are defined in <elf.h>, which are only available for C sources.
   X86_FEATURE_1_IBT and X86_FEATURE_1_SHSTK are defined in <sysdep.h>
   which are available for both C and asm sources.  They must match.   */
#if GNU_PROPERTY_X86_FEATURE_1_IBT != X86_FEATURE_1_IBT
# error GNU_PROPERTY_X86_FEATURE_1_IBT != X86_FEATURE_1_IBT
#endif
#if GNU_PROPERTY_X86_FEATURE_1_FINEIBT != X86_FEATURE_1_FINEIBT
# error GNU_PROPERTY_X86_FEATURE_1_FINEIBT != X86_FEATURE_1_FINEIBT
#endif
#if GNU_PROPERTY_X86_FEATURE_1_SHSTK != X86_FEATURE_1_SHSTK
# error GNU_PROPERTY_X86_FEATURE_1_SHSTK != X86_FEATURE_1_SHSTK
#endif


/* Check if object M is compatible with CET.  */

struct dl_info {
  /* Check how IBT, SHSTK should be enabled.  */
  enum dl_x86_cet_control enable_ibt_type;
  enum dl_x86_cet_control enable_shstk_type;
  enum dl_x86_cet_control enable_fineibt_type;

  /* Tell if IBT, SHSTK and FineIBT were previously enabled.  */
  bool ibt_enabled;
  bool shstk_enabled;
  bool fineibt_enabled;

  /* Tell if IBT, SHSTK and FineIBT should be enabled.  */
  bool enable_ibt;
  bool enable_shstk;
  bool enable_fineibt;

  /* Tell which shared object is the first legacy seen.  */
  int legacy_ibt;
  int legacy_shstk;
  int legacy_fineibt;
};

static void dl_check_legacy(struct link_map *m, struct dl_info *info,
                const char *program)
{
  unsigned int i;
  struct link_map *l = NULL;

  i = m->l_searchlist.r_nlist;
  while (i-- > 0)
    {
      /* Check each shared object to see if IBT and SHSTK are enabled.  */
      l = m->l_initfini[i];

      if (l->l_init_called)
        continue;

#ifdef SHARED
      /* Skip CET check for ld.so since ld.so is CET-enabled. CET will be
         disabled later if CET isn't enabled in executable.  */
      if (l == &GL(dl_rtld_map)
          ||  l->l_real == &GL(dl_rtld_map)
          || (program && l == m))
        continue;
#endif

      /* IBT/SHSTK set ON only if enabled in executable and all DSOs  */
      info->enable_ibt &= (info->enable_ibt_type == cet_always_on
                               || (l->l_cet & lc_ibt) != 0);
      if (!(info->legacy_ibt >= 0)
          && (info->enable_ibt != info->ibt_enabled))
        info->legacy_ibt = i;

      info->enable_fineibt &= (info->enable_fineibt_type == cet_always_on
                                   || (l->l_cet & lc_fineibt) != 0);
      if (!(info->legacy_fineibt >= 0)
          && (info->enable_fineibt != info->fineibt_enabled))
        info->legacy_fineibt = i;

      info->enable_shstk &= (info->enable_shstk_type == cet_always_on
                                 || (l->l_cet & lc_shstk) != 0);
      if (!(info->legacy_shstk >= 0)
          && (info->enable_shstk != info->shstk_enabled))
        info->legacy_shstk = i;
    }
}

/* dl_cet_check_main will set the bits properly in the start of the program */
static void
dl_cet_check_main(struct link_map *m, struct dl_info *info,
                  const char *program)
{
  /* Enable IBT and SHSTK only if they are enabled in executable.

     NB: IBT and SHSTK may be disabled by environment variable:
     GLIBC_TUNABLES=glibc.cpu.hwcaps=-IBT,-SHSTK  */
  info->enable_ibt &= (HAS_CPU_FEATURE (IBT)
                           && (info->enable_ibt_type == cet_always_on
                               || (m->l_cet & lc_ibt) != 0));

  info->enable_shstk &= (HAS_CPU_FEATURE (SHSTK)
                             && (info->enable_shstk_type == cet_always_on
                                 || (m->l_cet & lc_shstk) != 0));

  /* FineIBT needs special care here. It is considered enabled if set
   * in executable or flagged always on.  */
  info->fineibt_enabled = (info->enable_fineibt_type != cet_always_off);
  info->fineibt_enabled &= (info->enable_fineibt_type == cet_always_on
                            || (m->l_cet & lc_fineibt) != 0);
  info->enable_fineibt = info->fineibt_enabled;

  /* Check if there is any legacy object linked.  */
  if (info->enable_ibt || info->enable_shstk)
    dl_check_legacy(m, info, program);

  /* First handle IBT and SHSTK which need specific syscalls.  */
  unsigned int cet_feature = 0;
  if (info->enable_ibt
      && (info->enable_ibt_type == cet_always_on
          || !(info->legacy_ibt >= 0)))
        cet_feature |= GNU_PROPERTY_X86_FEATURE_1_IBT;

  if (info->enable_shstk
      && (info->enable_shstk_type == cet_always_on
          || !(info->legacy_shstk >= 0)))
        cet_feature |= GNU_PROPERTY_X86_FEATURE_1_SHSTK;

  /* Disable IBT and SHSTK in the kernel if needed.  */
  if (!info->enable_ibt || !info->enable_shstk)
    {
      /* Only the two last bits matter, thus switch all else off.  */
      int res = dl_cet_disable_cet(~cet_feature & 0x3);
      if (res)
        _dl_fatal_printf("%s: can't disable CET\n", program);
    }

#ifdef SHARED
  bool ibt_lock = false;
  bool shstk_lock = false;
  /* If IBT or SHSTK are enabled but permissive, CET can't be locked.  */
  if (info->ibt_enabled && info->enable_ibt_type != cet_permissive)
    ibt_lock = true;
  else if (!info->ibt_enabled)
    ibt_lock = true;

  if (info->shstk_enabled && info->enable_shstk_type != cet_permissive)
    shstk_lock = true;
  else if (!info->shstk_enabled)
    shstk_lock = true;

  if (ibt_lock && shstk_lock)
    {
      int res = dl_cet_lock_cet();
      if (res)
        _dl_fatal_printf("%s: can't lock CET\n", program);
    }
#endif

  /* Now evaluate FineIBT */
  if (info->enable_fineibt
      && (info->enable_fineibt_type == cet_always_on
          || !(info->legacy_fineibt >= 0)))
        cet_feature |= GNU_PROPERTY_X86_FEATURE_1_FINEIBT;

  /* And finally set the bits as required.  */
  if (info->enable_ibt || info->enable_shstk)
    {
      /* we just want to switch the last bits here.  */
      GL(dl_x86_feature_1) = cet_feature;
      struct pthread *self = THREAD_SELF;
      THREAD_SETMEM (self, header.feature_1, cet_feature);
    }
}

static void dl_cet_check_dyn(struct link_map *m, struct dl_info *info)
{
  /* Check if there is any legacy object linked.  */
  if (info->enable_ibt || info->enable_shstk)
    dl_check_legacy(m, info, NULL);

  unsigned int cet_feature = 0;
  unsigned int fineibt_feature = 0;

  if (info->fineibt_enabled && (info->legacy_fineibt >= 0))
    {
      if (info->enable_fineibt_type != cet_permissive)
        _dl_signal_error (0, m->l_initfini[info->legacy_ibt]->l_name,
                          "dlopen", N_("rebuild DSO with FineIBT enabled"));
      else
        fineibt_feature |= GNU_PROPERTY_X86_FEATURE_1_FINEIBT;
    }


  if (info->ibt_enabled && (info->legacy_ibt >= 0))
    {
      if (info->enable_ibt_type != cet_permissive)
        _dl_signal_error (0, m->l_initfini[info->legacy_ibt]->l_name,
                          "dlopen", N_("rebuild DSO with IBT enabled"));
      else {
        cet_feature |= GNU_PROPERTY_X86_FEATURE_1_IBT;
        fineibt_feature |= GNU_PROPERTY_X86_FEATURE_1_FINEIBT;
      }
    }

  if (info->shstk_enabled && (info->legacy_shstk >= 0))
    {
      if (info->enable_shstk_type != cet_permissive)
        _dl_signal_error(0, m->l_initfini[info->legacy_shstk]->l_name,
                         "dlopen", N_("rebuild DSO with SHSTK enabled"));
      else
        cet_feature |= GNU_PROPERTY_X86_FEATURE_1_SHSTK;
    }

  if (info->enable_ibt_type != cet_permissive
      && info->enable_shstk_type != cet_permissive
      && info->enable_fineibt_type != cet_permissive)
    return;

  /* Disable IBT and SHSTK in the kernel and set bits if needed.  */
  if (cet_feature)
    {
      int res = dl_cet_disable_cet(cet_feature);
      if (res)
        {
          const char *obj = "unknown";
          if (info->legacy_ibt >= 0)
            obj = m->l_initfini[info->legacy_ibt]->l_name;
          else if (info->legacy_shstk >= 0)
            obj = m->l_initfini[info->legacy_shstk]->l_name;
          else if (info->legacy_fineibt >= 0)
            obj = m->l_initfini[info->legacy_fineibt]->l_name;
          _dl_signal_error(-res, obj, "dlopen", N_("can't disable CET"));
        }
      }

    cet_feature |= fineibt_feature;
    if (cet_feature)
      {
        /* Only the two last bits matter here.  */
        GL(dl_x86_feature_1) &= (~cet_feature | 0xffffffe0);
        cet_feature = GL(dl_x86_feature_1);
        struct pthread *self = THREAD_SELF;
        THREAD_SETMEM (self, header.feature_1, cet_feature);
      }
}

static void
dl_cet_check (struct link_map *m, const char *program)
{
  /* Check how IBT, FineIBT and SHSTK should be enabled.  */
  struct dl_info info;
  info.enable_ibt_type = GL(dl_x86_feature_control).ibt;
  info.enable_shstk_type = GL(dl_x86_feature_control).shstk;
  info.enable_fineibt_type = GL(dl_x86_feature_control).fineibt;

  /* legacy_ibt/shstk/fineibt set to -1 in dl_cet_check_legacy if needed.  */
  info.legacy_ibt = -1;
  info.legacy_shstk = -1;
  info.legacy_fineibt = -1;

  /* No legacy object check if both IBT and SHSTK are always on.  */
  if (info.enable_ibt_type == cet_always_on
      && info.enable_fineibt_type == cet_always_on
      && info.enable_shstk_type == cet_always_on)
    return;

  /* Check if IBT and SHSTK were enabled by kernel.  */
  info.ibt_enabled
    = (GL(dl_x86_feature_1) & GNU_PROPERTY_X86_FEATURE_1_IBT) != 0;
  info.shstk_enabled
    = (GL(dl_x86_feature_1) & GNU_PROPERTY_X86_FEATURE_1_SHSTK) != 0;

  if (!info.ibt_enabled &&
      !info.shstk_enabled)
    return;

  info.enable_ibt = (info.ibt_enabled
                         && info.enable_ibt_type != cet_always_off);
  info.enable_shstk = (info.shstk_enabled
                           && info.enable_shstk_type != cet_always_off);

  if (program)
    dl_cet_check_main(m, &info, program);
  else
    dl_cet_check_dyn(m, &info);
}

void
_dl_cet_open_check (struct link_map *l)
{
  dl_cet_check (l, NULL);
}

#ifdef SHARED

# ifndef LINKAGE
#  define LINKAGE
# endif

LINKAGE
void
_dl_cet_check (struct link_map *main_map, const char *program)
{
  dl_cet_check (main_map, program);
}
#endif /* SHARED */
