{
	"patcher" : 	{
		"fileversion" : 1,
		"appversion" : 		{
			"major" : 8,
			"minor" : 6,
			"revision" : 0,
			"architecture" : "x64",
			"modernui" : 1
		},
		"classnamespace" : "box",
		"rect" : [ 100.0, 100.0, 640.0, 420.0 ],
		"bglocked" : 0,
		"openinpresentation" : 1,
		"default_fontsize" : 12.0,
		"default_fontface" : 0,
		"default_fontname" : "Arial",
		"gridonopen" : 1,
		"gridsize" : [ 15.0, 15.0 ],
		"gridsnaponopen" : 1,
		"objectsnaponopen" : 1,
		"statusbarvisible" : 2,
		"toolbarvisible" : 1,
		"boxes" : [
			{
				"box" : 				{
					"id" : "obj-1",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 3,
					"outlettype" : [ "bang", "int", "int" ],
					"patching_rect" : [ 30.0, 30.0, 92.0, 22.0 ],
					"text" : "live.thisdevice"
				}
			},
			{
				"box" : 				{
					"id" : "obj-2",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 2,
					"outlettype" : [ "", "" ],
					"patching_rect" : [ 30.0, 210.0, 140.0, 22.0 ],
					"saved_object_attributes" : 					{
						"filename" : "conduit_announce.js",
						"parameter_enable" : 0
					},
					"text" : "js conduit_announce.js"
				}
			},
			{
				"box" : 				{
					"id" : "obj-3",
					"maxclass" : "live.dial",
					"numinlets" : 1,
					"numoutlets" : 2,
					"outlettype" : [ "", "float" ],
					"parameter_enable" : 1,
					"patching_rect" : [ 180.0, 90.0, 44.0, 48.0 ],
					"presentation" : 1,
					"presentation_rect" : [ 15.0, 15.0, 44.0, 48.0 ],
					"saved_attribute_attributes" : 					{
						"valueof" : 						{
							"parameter_initial" : [ 0.25 ],
							"parameter_initial_enable" : 1,
							"parameter_longname" : "Rate",
							"parameter_mmax" : 4.0,
							"parameter_mmin" : 0.0625,
							"parameter_shortname" : "Rate",
							"parameter_type" : 0,
							"parameter_unitstyle" : 1
						}
					},
					"varname" : "Rate"
				}
			},
			{
				"box" : 				{
					"id" : "obj-4",
					"maxclass" : "live.dial",
					"numinlets" : 1,
					"numoutlets" : 2,
					"outlettype" : [ "", "float" ],
					"parameter_enable" : 1,
					"patching_rect" : [ 260.0, 90.0, 44.0, 48.0 ],
					"presentation" : 1,
					"presentation_rect" : [ 75.0, 15.0, 44.0, 48.0 ],
					"saved_attribute_attributes" : 					{
						"valueof" : 						{
							"parameter_initial" : [ 1.0 ],
							"parameter_initial_enable" : 1,
							"parameter_longname" : "Depth",
							"parameter_mmax" : 1.0,
							"parameter_mmin" : 0.0,
							"parameter_shortname" : "Depth",
							"parameter_type" : 0,
							"parameter_unitstyle" : 1
						}
					},
					"varname" : "Depth"
				}
			},
			{
				"box" : 				{
					"id" : "obj-5",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 180.0, 160.0, 82.0, 22.0 ],
					"text" : "prepend rate"
				}
			},
			{
				"box" : 				{
					"id" : "obj-6",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 280.0, 160.0, 90.0, 22.0 ],
					"text" : "prepend depth"
				}
			},
			{
				"box" : 				{
					"id" : "obj-7",
					"maxclass" : "live.numbox",
					"numinlets" : 1,
					"numoutlets" : 2,
					"outlettype" : [ "", "float" ],
					"parameter_enable" : 1,
					"patching_rect" : [ 420.0, 30.0, 80.0, 15.0 ],
					"saved_attribute_attributes" : 					{
						"valueof" : 						{
							"parameter_initial" : [ 0 ],
							"parameter_initial_enable" : 1,
							"parameter_invisible" : 1,
							"parameter_longname" : "RemoteSeed",
							"parameter_mmax" : 2147483000.0,
							"parameter_mmin" : 0.0,
							"parameter_shortname" : "RemoteSeed",
							"parameter_type" : 2,
							"parameter_unitstyle" : 0
						}
					},
					"varname" : "RemoteSeed"
				}
			},
			{
				"box" : 				{
					"id" : "obj-8",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 420.0, 90.0, 108.0, 22.0 ],
					"text" : "prepend seedvalue"
				}
			},
			{
				"box" : 				{
					"id" : "obj-9",
					"maxclass" : "newobj",
					"numinlets" : 1,
					"numoutlets" : 0,
					"outlettype" : [  ],
					"patching_rect" : [ 30.0, 330.0, 150.0, 22.0 ],
					"text" : "udpsend 127.0.0.1 9000"
				}
			},
			{
				"box" : 				{
					"id" : "obj-10",
					"maxclass" : "message",
					"numinlets" : 2,
					"numoutlets" : 1,
					"outlettype" : [ "" ],
					"patching_rect" : [ 210.0, 285.0, 130.0, 22.0 ],
					"text" : "host 192.168.0.42"
				}
			},
			{
				"box" : 				{
					"id" : "obj-11",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 350.0, 285.0, 260.0, 20.0 ],
					"text" : "Conduit auf anderem Rechner: IP anpassen + klicken"
				}
			},
			{
				"box" : 				{
					"id" : "obj-12",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 30.0, 60.0, 140.0, 20.0 ],
					"text" : "Live API bereit → announce"
				}
			},
			{
				"box" : 				{
					"id" : "obj-13",
					"maxclass" : "comment",
					"numinlets" : 1,
					"numoutlets" : 0,
					"patching_rect" : [ 505.0, 27.0, 130.0, 20.0 ],
					"text" : "remoteId (Stored Only)"
				}
			}
		],
		"lines" : [
			{
				"patchline" : 				{
					"destination" : [ "obj-2", 0 ],
					"source" : [ "obj-1", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-5", 0 ],
					"source" : [ "obj-3", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-6", 0 ],
					"source" : [ "obj-4", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-2", 0 ],
					"source" : [ "obj-5", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-2", 0 ],
					"source" : [ "obj-6", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-8", 0 ],
					"source" : [ "obj-7", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-2", 0 ],
					"source" : [ "obj-8", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-9", 0 ],
					"source" : [ "obj-2", 0 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-7", 0 ],
					"source" : [ "obj-2", 1 ]
				}
			},
			{
				"patchline" : 				{
					"destination" : [ "obj-9", 0 ],
					"source" : [ "obj-10", 0 ]
				}
			}
		],
		"dependency_cache" : [
			{
				"name" : "conduit_announce.js",
				"bootpath" : ".",
				"patcherrelativepath" : ".",
				"type" : "TEXT",
				"implicit" : 1
			}
		],
		"autosave" : 0
	}
}
