


typedef void cmd_f(char **av, void *priv);

struct cmds {
	const char	*name;
	cmd_f		*cmd;
};

void parse_string(char *buf, const struct cmds *cmd, void *priv);

/* vtc_server.c */
void cmd_server(char **av, void *priv);

