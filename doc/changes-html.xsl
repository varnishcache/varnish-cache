<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE stylesheet [
 <!ENTITY space "&#32;">
 <!ENTITY nbsp "&#160;">
]>
<!-- $Id$ -->
<xsl:stylesheet version="1.0"
 xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
 xmlns="http://www.w3.org/1999/xhtml">
  <xsl:output method="xml" encoding="utf-8" media-type="text/html" indent="yes"/>

  <xsl:strip-space elements="*"/>

  <xsl:template match="/changelog">
    <html>
      <head>
	<title><xsl:call-template name="title"/></title>
	<link rel="stylesheet" type="text/css" href="changes.css"/>
      </head>
      <body>
	<h1><xsl:call-template name="title"/></h1>
	<xsl:apply-templates select="group"/>
      </body>
    </html>
  </xsl:template>

  <xsl:template name="title">
    <xsl:text>Change log for&space;</xsl:text>
    <xsl:value-of select="package"/>
    <xsl:text>&space;</xsl:text>
    <xsl:value-of select="version"/>
  </xsl:template>

  <xsl:template match="group">
    <h2>
      <xsl:text>Changes between&space;</xsl:text>
      <xsl:value-of select="@from"/>
      <xsl:text>&space;and&space;</xsl:text>
      <xsl:value-of select="@to"/>
    </h2>
    <xsl:apply-templates select="subsystem"/>
  </xsl:template>

  <xsl:template match="subsystem">
    <h3>
      <xsl:value-of select="name"/>
    </h3>
    <ul>
      <xsl:apply-templates select="change"/>
    </ul>
  </xsl:template>

  <xsl:template match="change">
    <li>
      <xsl:apply-templates/>
    </li>
  </xsl:template>

  <xsl:template match="para">
    <p>
      <xsl:apply-templates/>
    </p>
  </xsl:template>

  <xsl:template match="ticket">
    <a>
      <xsl:attribute name="href">
	<xsl:text>http://varnish.projects.linpro.no/ticket/</xsl:text>
	<xsl:value-of select="@ref"/>
      </xsl:attribute>
      <xsl:text>ticket #</xsl:text>
      <xsl:value-of select="@ref"/>
    </a>
  </xsl:template>

  <xsl:template match="code">
    <span>
      <xsl:attribute name="class">
	<xsl:value-of select="name()"/>
      </xsl:attribute>
      <xsl:apply-templates/>
    </span>
  </xsl:template>

  <xsl:template match="*" priority="-1">
    <xsl:message>Warning: no template for element <xsl:value-of select="name(
)"/></xsl:message>
    <xsl:value-of select="concat('&lt;', name(), '&gt;')"/>
    <xsl:apply-templates/>
    <xsl:value-of select="concat('&lt;/', name(), '&gt;')"/>
  </xsl:template>
</xsl:stylesheet>
