<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE stylesheet [
 <!ENTITY lf "&#10;">
]>
<!-- $Id$ -->
<xsl:stylesheet version="1.0"
 xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
 xmlns="http://www.w3.org/1999/xhtml">
  <xsl:output method="text" encoding="utf-8"/>

  <xsl:strip-space elements="*"/>

  <xsl:template match="/changelog">
    <xsl:text>== </xsl:text>
    <xsl:call-template name="title"/>
    <xsl:text> ==&lf;</xsl:text>
    <xsl:apply-templates select="group"/>
  </xsl:template>

  <xsl:template name="title">
    <xsl:text>Change log for </xsl:text>
    <xsl:value-of select="package"/>
    <xsl:text> </xsl:text>
    <xsl:value-of select="version"/>
  </xsl:template>

  <xsl:template match="group">
    <xsl:text>=== </xsl:text>
    <xsl:text>Changes between </xsl:text>
    <xsl:value-of select="@from"/>
    <xsl:text> and </xsl:text>
    <xsl:value-of select="@to"/>
    <xsl:text> ===&lf;</xsl:text>
    <xsl:apply-templates select="subsystem"/>
  </xsl:template>

  <xsl:template match="subsystem">
    <xsl:text>==== </xsl:text>
    <xsl:value-of select="name"/>
    <xsl:text> ====&lf;</xsl:text>
    <xsl:apply-templates select="change"/>
  </xsl:template>

  <xsl:template match="change">
    <xsl:text> * </xsl:text>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="para">
    <xsl:apply-templates/>
    <xsl:text>&lf;</xsl:text>
  </xsl:template>

  <xsl:template match="ticket">
    <xsl:text>#</xsl:text>
    <xsl:value-of select="@ref"/>
  </xsl:template>

  <xsl:template match="code">
    <xsl:text> {{{</xsl:text>
    <xsl:apply-templates/>
    <xsl:text>}}} </xsl:text>
  </xsl:template>

  <xsl:template match="text()">
    <xsl:value-of select="normalize-space()"/>
  </xsl:template>

  <xsl:template match="*" priority="-1">
    <xsl:message>Warning: no template for element <xsl:value-of select="name(
)"/></xsl:message>
    <xsl:value-of select="concat('&lt;', name(), '&gt;')"/>
    <xsl:apply-templates/>
    <xsl:value-of select="concat('&lt;/', name(), '&gt;')"/>
  </xsl:template>
</xsl:stylesheet>
