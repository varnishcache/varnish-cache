


typedef void cmd_f(char **av, void *priv);

struct cmds {
	const char	*name;
	cmd_f		*cmd;
};

void parse_string(char *buf, const struct cmds *cmd, void *priv);

void cmd_dump(char **av, void *priv);
void cmd_server(char **av, void *priv);
void cmd_client(char **av, void *priv);
void cmd_vcl(char **av, void *priv);
void cmd_stats(char **av, void *priv);
void cmd_varnish(char **av, void *priv);

