struct vcl {
	unsigned		magic;
#define VCL_MAGIC		0x214188f2
	VTAILQ_ENTRY(vcl)	list;
	void			*dlh;
	const struct VCL_conf	*conf;
	char			state[8];
	char			*loaded_name;
	unsigned		busy;
	unsigned		discard;
	const char		*temp;
	pthread_rwlock_t	temp_rwl;
	VTAILQ_HEAD(,backend)	backend_list;
	VTAILQ_HEAD(,vclref)	ref_list;
};
