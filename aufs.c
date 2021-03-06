#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/debugfs.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
#include <linux/cred.h>

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/debugfs.h>
#include <linux/fsnotify.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/srcu.h>

struct dentry *aufs_create_file(const char *name, mode_t mode,
				   struct dentry *parent, void *data,
				   struct file_operations *fops);


#define AUFS_MAGIC	0x64626720

static struct vfsmount *aufs_mount;
static int aufs_mount_count;

static struct inode *aufs_get_inode(struct super_block *sb, int mode, dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		//inode->i_uid = current->fsuid;
		inode->i_uid = current_fsuid();
		inode->i_gid = current_fsgid();
		inode->i_blkbits = 4096;
		inode->i_blocks = 0;
		//inode->i_mapping->a_ops = &aufs_aops;
		//inode->i_mapping->backing_dev_info = &aufs_backing_dev_info;
		
		//inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			//inode->i_fop = &aufs_file_operations;
			printk("creat a  file \n");
			break;
		case S_IFDIR:
			inode->i_op = &simple_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			printk("creat a dir file \n");
			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inode->__i_nlink++;
			break;
		}
	}
	return inode; 
}


/* SMP-safe */
static int aufs_mknod(struct inode *dir, struct dentry *dentry,
			 int mode, dev_t dev)
{
	struct inode *inode;
	int error = -EPERM;

	if (dentry->d_inode)
		return -EEXIST;

	inode = aufs_get_inode(dir->i_sb, mode, dev);
	if (inode) {
        	if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}

		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	return error;
}


static int aufs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int res;

	res = aufs_mknod(dir, dentry, mode |S_IFDIR, 0);
	if (!res)
		dir->__i_nlink++;
	return res;
}

static int aufs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return aufs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int aufs_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr debug_files[] = {{""}};

	simple_fill_super(sb, AUFS_MAGIC, debug_files);

	return 0;
}

static struct dentry *aufs_mount_func(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, aufs_fill_super);
}

#if 0
static struct super_block *aufs_get_sb(struct file_system_type *fs_type,
				        int flags, const char *dev_name,
					void *data)
{
	return get_sb_single(fs_type, flags, data, aufs_fill_super);
}
#endif

static struct file_system_type au_fs_type = {
	.owner 		= THIS_MODULE,
	.name 		= "aufs",
	//.get_sb =	aufs_get_sb,
	.mount		= aufs_mount_func,
	.kill_sb 	= kill_litter_super,
};

/*static struct inode_operations  aufs_dir_inode_operations = {
	.create		= aufs_create,
	.lookup		= simple_lookup,
	.mkdir		= aufs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= aufs_mknod,
};*/


static int aufs_create_by_name(const char *name, mode_t mode,
				  struct dentry *parent,
				  struct dentry **dentry)
{
	int error = 0;

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super 
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent ) {
		if (aufs_mount && aufs_mount->mnt_sb) {
			parent = aufs_mount->mnt_sb->s_root;
		}
	}
	if (!parent) {
		printk("aufs: Ah! can not find a parent!\n");
		return -EFAULT;
	}

	*dentry = NULL;
	mutex_lock(&d_inode(parent)->i_mutex);
	*dentry = lookup_one_len(name, parent, strlen(name));
	
	if (!IS_ERR(dentry)) {
		if ((mode & S_IFMT) == S_IFDIR)
			error = aufs_mkdir(parent->d_inode, *dentry, mode);
		else 
			error = aufs_create(parent->d_inode, *dentry, mode);
	} else
		error = PTR_ERR(dentry);
	mutex_unlock(&d_inode(parent)->i_mutex);

	return error;
}

struct dentry *aufs_create_file(const char *name, mode_t mode,
				   struct dentry *parent, void *data,
				   struct file_operations *fops)
{
	struct dentry *dentry = NULL;
	int error;

	printk("aufs: creating file '%s'\n", name);

	error = simple_pin_fs(&au_fs_type, &aufs_mount, &aufs_mount_count);
	
	if (error)
		goto exit;

	error = aufs_create_by_name(name, mode, parent, &dentry);
	if (error) {
		dentry = NULL;
		goto exit;
	}

	if (dentry->d_inode) {
		//if (data)
			//dentry->d_inode->u.generic_ip = data;
		if (fops)
			dentry->d_inode->i_fop = fops;
	}
exit:
	return dentry;
}
EXPORT_SYMBOL_GPL(aufs_create_file);

	
struct dentry *aufs_create_dir(const char *name, struct dentry *parent)
{
	return aufs_create_file(name, 
				   S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
				   parent, NULL, NULL);
}
EXPORT_SYMBOL_GPL(aufs_create_dir);


static int __init aufs_init(void)
{
	struct dentry *pslot = NULL;
	int retval = 0;

	//retval = sysfs_create_mount_point(kernel_kobj, "aufs");
	//printk("sysfs_create_mount_point ret is: %d.\n", retval);
	
	retval = register_filesystem(&au_fs_type);
	printk("register_filesystem ret is: %d.\n", retval);

	pslot = aufs_create_file("file", S_IFREG | S_IRUGO, NULL, NULL, NULL);
	if(!pslot) 
		 printk(KERN_WARNING "aufs_create_file() failed!");

#if 0
	struct dentry *pslot;
	struct block_device *bdev;		
	struct inode *		bd_inode;
	struct super_block	*i_sb;
	struct dentry		*s_root;
	struct list_head       *head;
	struct dentry           *de;
	struct inode            *inode;	
	struct gendisk *	bd_disk;

	if (!retval) {
		aufs_mount = kern_mount(&au_fs_type);
		if (IS_ERR(aufs_mount)) {
			printk(KERN_ERR "aufs: could not mount!\n");
			unregister_filesystem(&au_fs_type);
			return retval;
		}
	} 

 //	nr_free_pagecache_pages();
//	nr_free_highpages
//	nr_free_buffer_pages
 	printk("total pages:%d\n",nr_free_pagecache_pages());
    printk("total buffer pages:%d\n",nr_free_buffer_pages()); 
    printk("total high pages:%d\n",nr_free_highpages());

	bdev = open_by_devnum(MKDEV(8,0),FMODE_READ|FMODE_WRITE);
	if (IS_ERR(bdev)) {
		printk(KERN_ERR "could not open %s.\n");
		return -1;
	}
	bd_inode = bdev->bd_inode;
	i_sb = bd_inode->i_sb;
        s_root = i_sb->s_root;

        printk("find a dentry\n");
        printk("name :%s \n",s_root->d_name.name);			 
        printk("mounted :%d\n",s_root->d_mounted);

        if(!list_empty(&s_root->d_subdirs))
            printk("bdev have child \n");			
	else 
            printk("bdev no child \n");

	list_for_each(head,&i_sb->s_inodes)
	{
             inode = list_entry(head, struct inode, i_sb_list);
	     bdev = inode->i_bdev;
	     bd_disk = bdev->bd_disk;
		 
             printk("find a dev inode\n");
             printk("name :%s \n",bd_disk->disk_name);
			 
	}
		
    pslot = aufs_create_dir("woman star",NULL);
	aufs_create_file("lbb", S_IFREG | S_IRUGO, pslot, NULL, NULL);	
	aufs_create_file("fbb", S_IFREG | S_IRUGO, pslot, NULL, NULL);
	aufs_create_file("ljl", S_IFREG | S_IRUGO, pslot, NULL, NULL);	

	pslot = aufs_create_dir("man star",NULL);		
	aufs_create_file("ldh", S_IFREG | S_IRUGO, pslot, NULL, NULL);	
	aufs_create_file("lcw", S_IFREG | S_IRUGO, pslot, NULL, NULL);		
	aufs_create_file("jw", S_IFREG | S_IRUGO, pslot, NULL, NULL);


	pslot = aufs_create_file("slot1", S_IFREG | S_IRUGO, NULL, NULL, NULL);
	if(!pslot) 
		 printk(KERN_WARNING "aufs_create_file() failed!");

	pslot = aufs_create_file("slot2", S_IFREG | S_IRUGO, NULL, NULL, NULL);
	if(!pslot) 
		 printk(KERN_WARNING "aufs_create_file() failed!");

	pslot = aufs_create_file("slot3", S_IFREG | S_IRUGO, NULL, NULL, NULL);
	if(!pslot) 
		 printk(KERN_WARNING "aufs_create_file() failed!");
#endif

	return retval;
}

static void __exit aufs_exit(void)
{
	simple_release_fs(&aufs_mount, &aufs_mount_count);
	unregister_filesystem(&au_fs_type);
}

module_init(aufs_init);
module_exit(aufs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("");
MODULE_DESCRIPTION("This is a simple module");
MODULE_VERSION("Ver 0.1");

