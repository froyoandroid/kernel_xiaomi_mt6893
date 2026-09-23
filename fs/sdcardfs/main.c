/*
 * fs/sdcardfs/main.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"
#include <linux/fscrypt.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>

enum {
	Opt_fsuid,
	Opt_fsgid,
	Opt_gid,
	Opt_debug,
	Opt_mask,
	Opt_multiuser,
	Opt_userid,
	Opt_reserved_mb,
	Opt_gid_derivation,
	Opt_default_normal,
	Opt_nocache,
	Opt_unshared_obb,
};

struct sdcardfs_fs_context {
	struct sdcardfs_mount_options options;
	struct sdcardfs_vfsmount_options vfsopts;
	unsigned int vfsopts_set;
};

static const struct fs_parameter_spec sdcardfs_param_specs[] = {
	fsparam_u32("fsuid", Opt_fsuid),
	fsparam_u32("fsgid", Opt_fsgid),
	fsparam_u32("gid", Opt_gid),
	fsparam_flag("debug", Opt_debug),
	fsparam_u32("mask", Opt_mask),
	fsparam_u32("userid", Opt_userid),
	fsparam_flag("multiuser", Opt_multiuser),
	fsparam_u32("reserved_mb", Opt_reserved_mb),
	fsparam_flag("derive_gid", Opt_gid_derivation),
	fsparam_flag("default_normal", Opt_default_normal),
	fsparam_flag("unshared_obb", Opt_unshared_obb),
	fsparam_flag("nocache", Opt_nocache),
	{}
};

static const struct fs_parameter_description sdcardfs_parameters = {
	.name = SDCARDFS_NAME,
	.specs = sdcardfs_param_specs,
};

static int sdcardfs_parse_param(struct fs_context *fc,
			      struct fs_parameter *param)
{
	struct sdcardfs_fs_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, &sdcardfs_parameters, param, &result);
	if (opt < 0)
		return opt;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE &&
	    opt != Opt_gid && opt != Opt_mask && opt != Opt_debug) {
		warnf(fc, "sdcardfs: %s cannot be changed during remount\n",
		      param->key);
		return 0;
	}

	switch (opt) {
	case Opt_fsuid:
		ctx->options.fs_low_uid = result.uint_32;
		break;
	case Opt_fsgid:
		ctx->options.fs_low_gid = result.uint_32;
		break;
	case Opt_gid:
		ctx->vfsopts.gid = result.uint_32;
		ctx->vfsopts_set |= BIT(Opt_gid);
		break;
	case Opt_mask:
		ctx->vfsopts.mask = result.uint_32;
		ctx->vfsopts_set |= BIT(Opt_mask);
		break;
	case Opt_userid:
		ctx->options.fs_user_id = result.uint_32;
		break;
	case Opt_multiuser:
		ctx->options.multiuser = true;
		break;
	case Opt_reserved_mb:
		ctx->options.reserved_mb = result.uint_32;
		break;
	case Opt_gid_derivation:
		ctx->options.gid_derivation = true;
		break;
	case Opt_default_normal:
		ctx->options.default_normal = true;
		break;
	case Opt_unshared_obb:
		ctx->options.unshared_obb = true;
		break;
	case Opt_nocache:
		ctx->options.nocache = true;
		break;
	case Opt_debug:
		break;
	}
	return 0;
}

void sdcardfs_update_mnt_data(void *data, struct fs_context *fc)
{
	struct sdcardfs_vfsmount_options *opts = data;
	struct sdcardfs_fs_context *ctx = fc->fs_private;

	/* A remount changes only the options supplied for this mount. */
	if (fc->purpose != FS_CONTEXT_FOR_RECONFIGURE) {
		*opts = ctx->vfsopts;
		return;
	}
	if (ctx->vfsopts_set & BIT(Opt_gid))
		opts->gid = ctx->vfsopts.gid;
	if (ctx->vfsopts_set & BIT(Opt_mask))
		opts->mask = ctx->vfsopts.mask;
}

#if 0
/*
 * our custom d_alloc_root work-alike
 *
 * we can't use d_alloc_root if we want to use our own interpose function
 * unchanged, so we simply call our own "fake" d_alloc_root
 */
static struct dentry *sdcardfs_d_alloc_root(struct super_block *sb)
{
	struct dentry *ret = NULL;

	if (sb) {
		static const struct qstr name = {
			.name = "/",
			.len = 1
		};

		ret = d_alloc(NULL, &name);
		if (ret) {
			d_set_d_op(ret, &sdcardfs_ci_dops);
			ret->d_sb = sb;
			ret->d_parent = ret;
		}
	}
	return ret;
}
#endif

DEFINE_MUTEX(sdcardfs_super_list_lock);
EXPORT_SYMBOL_GPL(sdcardfs_super_list_lock);
LIST_HEAD(sdcardfs_super_list);
EXPORT_SYMBOL_GPL(sdcardfs_super_list);

/*
 * There is no need to lock the sdcardfs_super_info's rwsem as there is no
 * way anyone can have a reference to the superblock at this point in time.
 */
static int sdcardfs_read_super(struct super_block *sb, struct fs_context *fc)
{
	int err = 0;
	struct sdcardfs_fs_context *ctx = fc->fs_private;
	const char *dev_name = fc->source;
	bool silent = fc->sb_flags & SB_SILENT;
	struct super_block *lower_sb;
	struct path lower_path;
	struct sdcardfs_sb_info *sb_info;
	struct inode *inode;

	pr_info("sdcardfs version 2.0\n");

	if (!dev_name) {
		pr_err("sdcardfs: read_super: missing dev_name argument\n");
		err = -EINVAL;
		goto out;
	}

	pr_info("sdcardfs: dev_name -> %s\n", dev_name);

	/* parse lower path */
	err = kern_path(dev_name, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
			&lower_path);
	if (err) {
		pr_err("sdcardfs: error accessing lower directory '%s'\n", dev_name);
		goto out;
	}

	/* allocate superblock private data */
	sb->s_fs_info = kzalloc(sizeof(struct sdcardfs_sb_info), GFP_KERNEL);
	if (!SDCARDFS_SB(sb)) {
		pr_crit("sdcardfs: read_super: out of memory\n");
		err = -ENOMEM;
		goto out_free;
	}

	sb_info = sb->s_fs_info;
	sb_info->options = ctx->options;

	/* set the lower superblock field of upper superblock */
	lower_sb = lower_path.dentry->d_sb;
	atomic_inc(&lower_sb->s_active);
	sdcardfs_set_lower_super(sb, lower_sb);

	sb->s_stack_depth = lower_sb->s_stack_depth + 1;
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("sdcardfs: maximum fs stacking depth exceeded\n");
		err = -EINVAL;
		goto out_sput;
	}

	/* inherit maxbytes from lower file system */
	sb->s_maxbytes = lower_sb->s_maxbytes;

	/*
	 * Our c/m/atime granularity is 1 ns because we may stack on file
	 * systems whose granularity is as good.
	 */
	sb->s_time_gran = 1;

	sb->s_magic = SDCARDFS_SUPER_MAGIC;
	sb->s_op = &sdcardfs_sops;

	/* get a new inode and allocate our root dentry */
	inode = sdcardfs_iget(sb, d_inode(lower_path.dentry), 0);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_sput;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto out_sput;
	}
	d_set_d_op(sb->s_root, &sdcardfs_ci_dops);

	/* link the upper and lower dentries */
	sb->s_root->d_fsdata = NULL;
	err = new_dentry_private_data(sb->s_root);
	if (err)
		goto out_freeroot;

	/* set the lower dentries for s_root */
	path_get(&lower_path);
	sdcardfs_set_lower_path(sb->s_root, &lower_path);

	/*
	 * No need to call interpose because we already have a positive
	 * dentry, which was instantiated by d_make_root.  Just need to
	 * d_rehash it.
	 */
	d_rehash(sb->s_root);

	/* setup permission policy */
	sb_info->obbpath_s = kzalloc(PATH_MAX, GFP_KERNEL);
	if (!sb_info->obbpath_s) {
		err = -ENOMEM;
		goto out_freeroot;
	}
	mutex_lock(&sdcardfs_super_list_lock);
	if (sb_info->options.multiuser) {
		setup_derived_state(d_inode(sb->s_root), PERM_PRE_ROOT,
				sb_info->options.fs_user_id, AID_ROOT);
		snprintf(sb_info->obbpath_s, PATH_MAX, "%s/obb", dev_name);
	} else {
		setup_derived_state(d_inode(sb->s_root), PERM_ROOT,
				sb_info->options.fs_user_id, AID_ROOT);
		snprintf(sb_info->obbpath_s, PATH_MAX, "%s/Android/obb", dev_name);
	}
	fixup_tmp_permissions(d_inode(sb->s_root));
	sb_info->sb = sb;
	list_add(&sb_info->list, &sdcardfs_super_list);
	mutex_unlock(&sdcardfs_super_list_lock);

	sb_info->fscrypt_nb.notifier_call = sdcardfs_on_fscrypt_key_removed;
	fscrypt_register_key_removal_notifier(&sb_info->fscrypt_nb);

	if (!silent)
		pr_info("sdcardfs: mounted on top of %s type %s\n",
				dev_name, lower_sb->s_type->name);
	path_put(&lower_path);
	goto out; /* all is well */

	/* no longer needed: free_dentry_private_data(sb->s_root); */
out_freeroot:
	d_drop(sb->s_root);
	dput(sb->s_root);
	sb->s_root = NULL;
out_sput:
	/* drop refs we took earlier */
	atomic_dec(&lower_sb->s_active);
	kfree(SDCARDFS_SB(sb));
	sb->s_fs_info = NULL;
out_free:
	path_put(&lower_path);

out:
	return err;
}

static int sdcardfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, sdcardfs_read_super);
}

static int sdcardfs_reconfigure(struct fs_context *fc)
{
	if (fc->sb_flags & ~(SB_RDONLY | SB_MANDLOCK | SB_SILENT))
		return invalf(fc, "sdcardfs: unsupported remount flags\n");
	return 0;
}

static void sdcardfs_free_fs_context(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations sdcardfs_context_ops = {
	.free = sdcardfs_free_fs_context,
	.parse_param = sdcardfs_parse_param,
	.get_tree = sdcardfs_get_tree,
	.reconfigure = sdcardfs_reconfigure,
};

static int sdcardfs_init_fs_context(struct fs_context *fc)
{
	struct sdcardfs_fs_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->options.fs_low_uid = AID_MEDIA_RW;
	ctx->options.fs_low_gid = AID_MEDIA_RW;
	fc->fs_private = ctx;
	fc->ops = &sdcardfs_context_ops;
	return 0;
}

void *sdcardfs_alloc_mnt_data(void)
{
	return kzalloc(sizeof(struct sdcardfs_vfsmount_options), GFP_KERNEL);
}

void sdcardfs_kill_sb(struct super_block *sb)
{
	struct sdcardfs_sb_info *sbi;

	if (sb->s_magic == SDCARDFS_SUPER_MAGIC && sb->s_fs_info) {
		sbi = SDCARDFS_SB(sb);

		fscrypt_unregister_key_removal_notifier(&sbi->fscrypt_nb);

		mutex_lock(&sdcardfs_super_list_lock);
		list_del(&sbi->list);
		mutex_unlock(&sdcardfs_super_list_lock);
	}
	kill_anon_super(sb);
}

static struct file_system_type sdcardfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= SDCARDFS_NAME,
	.init_fs_context = sdcardfs_init_fs_context,
	.parameters	= &sdcardfs_parameters,
	.alloc_mnt_data = sdcardfs_alloc_mnt_data,
	.kill_sb	= sdcardfs_kill_sb,
	.fs_flags	= 0,
};
MODULE_ALIAS_FS(SDCARDFS_NAME);

static int __init init_sdcardfs_fs(void)
{
	int err;

	pr_info("Registering sdcardfs " SDCARDFS_VERSION "\n");

	err = sdcardfs_init_inode_cache();
	if (err)
		return err;
	err = sdcardfs_init_dentry_cache();
	if (err)
		goto out_inode_cache;
	err = packagelist_init();
	if (err)
		goto out_dentry_cache;
	err = register_filesystem(&sdcardfs_fs_type);
	if (!err)
		return 0;
	packagelist_exit();
out_dentry_cache:
	sdcardfs_destroy_dentry_cache();
out_inode_cache:
	sdcardfs_destroy_inode_cache();
	return err;
}

static void __exit exit_sdcardfs_fs(void)
{
	unregister_filesystem(&sdcardfs_fs_type);
	packagelist_exit();
	rcu_barrier();
	sdcardfs_destroy_inode_cache();
	sdcardfs_destroy_dentry_cache();
	pr_info("Completed sdcardfs module unload\n");
}

/* Original wrapfs authors */
MODULE_AUTHOR("Erez Zadok, Filesystems and Storage Lab, Stony Brook University (http://www.fsl.cs.sunysb.edu/)");

/* Original sdcardfs authors */
MODULE_AUTHOR("Woojoong Lee, Daeho Jeong, Kitae Lee, Yeongjin Gil System Memory Lab., Samsung Electronics");

/* Current maintainer */
MODULE_AUTHOR("Daniel Rosenberg, Google");
MODULE_DESCRIPTION("Sdcardfs " SDCARDFS_VERSION);
MODULE_LICENSE("GPL");

module_init(init_sdcardfs_fs);
module_exit(exit_sdcardfs_fs);
