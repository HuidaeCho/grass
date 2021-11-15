from grass.gunittest.case import TestCase
from grass.gunittest.main import test
from grass.gunittest.gmodules import SimpleModule, call_module

from grass.script.core import tempname
from grass.pygrass.gis import Mapset
from grass.pygrass.raster import RasterRow


class TestSemanticLabelsSystemDefined(TestCase):
    @classmethod
    def setUpClass(cls):
        cls.map = tempname(10)
        cls.semantic_label = "The_Doors"
        cls.mapset = Mapset()
        cls.use_temp_region()
        cls.runModule("g.region", n=1, s=0, e=1, w=0, res=1)
        cls.runModule("r.mapcalc", expression="{} = 1".format(cls.map))

    @classmethod
    def tearDownClass(cls):
        cls.del_temp_region()
        cls.runModule("g.remove", flags="f", type="raster", name=cls.map)

    def read_semantic_label(self):
        with RasterRow(self.map) as rast:
            semantic_label = rast.info.semantic_label

        return semantic_label

    def test_semantic_label_assign_not_current_mapset(self):
        if not self.mapset == "PERMANENT":
            self.mapset.name = "PERMANENT"
            a_map = self.mapset.glist(type="raster")[0]
            module = SimpleModule(
                "i.band", map=a_map, semantic_label=self.semantic_label
            )
            self.assertModuleFail(module)

    def test_semantic_label_assign(self):
        module = SimpleModule(
            "i.band", map=self.map, semantic_label=self.semantic_label
        )
        self.assertModule(module)

        # check also using pygrass
        self.assertEqual(self.read_semantic_label(), self.semantic_label)

    def test_semantic_label_dissociate(self):
        module = SimpleModule("i.band", operation="remove", map=self.map)
        self.assertModule(module)

        # check also using pygrass
        self.assertEqual(self.read_semantic_label(), None)


if __name__ == "__main__":
    test()
